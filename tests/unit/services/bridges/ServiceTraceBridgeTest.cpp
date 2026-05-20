#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/AdapterBridge.h"
#include "bridges/ServiceTraceBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCode;
using dasall::infra::tracing::ActiveSpanCallback;
using dasall::infra::tracing::ISpan;
using dasall::infra::tracing::ITracer;
using dasall::infra::tracing::ITracerProvider;
using dasall::infra::tracing::SpanDescriptor;
using dasall::infra::tracing::SpanEndResult;
using dasall::infra::tracing::SpanKind;
using dasall::infra::tracing::SpanStatusCode;
using dasall::infra::tracing::TraceAttributeMap;
using dasall::infra::tracing::TraceAttributeValue;
using dasall::infra::tracing::TraceConfig;
using dasall::infra::tracing::TraceContext;
using dasall::infra::tracing::TraceContextState;
using dasall::infra::tracing::TraceOperationStatus;
using dasall::infra::tracing::TracerScope;
using dasall::services::CapabilityTargetRef;
using dasall::services::DataQueryResult;
using dasall::services::ExecutionSubscriptionResult;
using dasall::services::ServiceCallContext;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterReceipt;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterSelection;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::ServiceTraceBridge;
using dasall::services::internal::ServiceTraceBridgeOptions;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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
  RecordingSpan(SpanDescriptor descriptor,
                TraceContext context,
                std::optional<TraceContext> parent_context)
      : descriptor(std::move(descriptor)),
        context(std::move(context)),
        parent_context(std::move(parent_context)) {}

  void set_attribute(std::string_view key,
                     const TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(std::string_view name,
                 const TraceAttributeMap&) override {
    events.push_back(std::string(name));
  }

  void set_status(SpanStatusCode code,
                  std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  [[nodiscard]] SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    ++end_call_total;
    if (!end_result.has_value()) {
      end_result = SpanEndResult{
          .end_ts_unix_ms = end_ts_unix_ms.has_value()
                               ? end_ts_unix_ms
                               : std::optional<std::int64_t>(1712736020000LL),
          .status_code = status_code,
          .status_message = status_message,
          .dropped_attr_count = 0U,
      };
    }

    return *end_result;
  }

  [[nodiscard]] TraceContext get_context() const override {
    return context;
  }

  SpanDescriptor descriptor;
  TraceContext context;
  std::optional<TraceContext> parent_context;
  TraceAttributeMap attributes;
  std::vector<std::string> events;
  SpanStatusCode status_code = SpanStatusCode::Unset;
  std::string status_message;
  int end_call_total = 0;
  std::optional<SpanEndResult> end_result;
};

struct StartedSpanRecord {
  SpanDescriptor descriptor;
  std::optional<TraceContext> explicit_parent;
  std::optional<TraceContext> resolved_parent;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public ITracer {
 public:
  [[nodiscard]] std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) override {
    std::optional<TraceContext> explicit_parent = std::nullopt;
    if (parent != nullptr) {
      explicit_parent = *parent;
    }

    std::optional<TraceContext> resolved_parent = std::nullopt;
    if (parent != nullptr && parent->state == TraceContextState::Active) {
      resolved_parent = *parent;
    } else if (active_context_.state == TraceContextState::Active) {
      resolved_parent = active_context_;
    }

    TraceContext context{
        .trace_id = resolved_parent.has_value()
                        ? resolved_parent->trace_id
                        : hex_id(++trace_seed_,
                                 dasall::infra::tracing::kTraceIdHexLength),
        .span_id = hex_id(++span_seed_,
                          dasall::infra::tracing::kSpanIdHexLength),
        .trace_flags = 0x01U,
        .trace_state = {},
        .parent_span_id = resolved_parent.has_value() ? resolved_parent->span_id
                                                      : std::string{},
        .state = TraceContextState::Active,
        .is_remote = false,
    };
    auto span = std::make_shared<RecordingSpan>(descriptor,
                                                context,
                                                resolved_parent);
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .explicit_parent = explicit_parent,
        .resolved_parent = resolved_parent,
        .span = span,
    });
    return span;
  }

  void with_active_span(const std::shared_ptr<ISpan>& span,
                        const ActiveSpanCallback& fn) override {
    const auto recording_span = std::dynamic_pointer_cast<RecordingSpan>(span);
    const auto previous = active_context_;
    active_context_ = recording_span != nullptr ? recording_span->context
                                                : TraceContext::noop();
    fn();
    active_context_ = previous;
  }

  [[nodiscard]] TraceContext current_context() const override {
    return active_context_;
  }

  std::vector<StartedSpanRecord> started_spans;

 private:
  TraceContext active_context_ = TraceContext::noop();
  std::uint64_t trace_seed_ = 0U;
  std::uint64_t span_seed_ = 0U;
};

class RecordingTracerProvider final : public ITracerProvider {
 public:
  TraceOperationStatus init(const TraceConfig&) override {
    return TraceOperationStatus::success("trace://services/provider-init");
  }

  [[nodiscard]] std::shared_ptr<ITracer> get_tracer(
      const TracerScope& scope) override {
    last_scope = scope;
    ++get_tracer_call_total;
    if (tracer == nullptr) {
      tracer = std::make_shared<RecordingTracer>();
    }
    return tracer;
  }

  TraceOperationStatus force_flush(std::uint32_t) override {
    return TraceOperationStatus::success("trace://services/provider-flush");
  }

  TraceOperationStatus shutdown(std::uint32_t) override {
    return TraceOperationStatus::success("trace://services/provider-shutdown");
  }

  std::shared_ptr<RecordingTracer> tracer;
  TracerScope last_scope{};
  std::uint64_t get_tracer_call_total = 0U;
};

[[nodiscard]] const std::string* string_attr(const TraceAttributeMap& attrs,
                                             std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<std::string>(attr) : nullptr;
}

[[nodiscard]] const bool* bool_attr(const TraceAttributeMap& attrs,
                                    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<bool>(attr) : nullptr;
}

[[nodiscard]] const std::int64_t* int64_attr(const TraceAttributeMap& attrs,
                                             std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<std::int64_t>(attr) : nullptr;
}

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000U;

  return ServiceCallContext{
      .request_id = "req-026",
      .session_id = "session-026",
      .trace_id = "trace-026-tool",
      .tool_call_id = "tool-call-026",
      .goal_id = "goal-026",
      .budget_guard = budget,
      .deadline_ms = 12000U,
  };
}

[[nodiscard]] CapabilityTargetRef make_target() {
  return CapabilityTargetRef{
      .capability_id = "devices",
      .target_id = "devices",
  };
}

[[nodiscard]] AdapterSelection make_selection() {
  return AdapterSelection{
      .route_kind = AdapterRouteKind::local_service,
      .adapter_id = "service-primary",
      .target_id = "target-026",
      .route_equivalence_class = "service.local",
      .fallback_hop = 0U,
      .selected_reason = "preferred_route_selected",
      .trust_class = dasall::services::internal::AdapterTrustClass::caller_verified,
      .availability_state = dasall::services::internal::AdapterAvailabilityState::available,
  };
}

[[nodiscard]] AdapterInvocationRequest make_adapter_request() {
  return AdapterInvocationRequest{
      .request_id = "req-026-adapter",
      .capability_id = "cap.exec",
      .target_id = "target-026",
      .request_kind = AdapterRouteRequestKind::action,
      .operation_name = "toggle",
      .payload_json = "{}",
  };
}

void test_service_trace_bridge_starts_facade_span_with_remote_parent_context() {
  auto provider = std::make_shared<RecordingTracerProvider>();
  ServiceTraceBridge bridge(provider,
                            ServiceTraceBridgeOptions{
                                .enabled = true,
                                .profile_id = "edge_balanced",
                                .trace_sample_ratio = 1.0,
                            });

  auto span = bridge.start_facade_span(make_context(), "execute");
  bridge.mark_success(&span);

  assert_true(span.is_valid(),
              "service trace bridge should materialize a valid facade span when a tracer provider is configured");
  assert_true(bridge.has_active_tracer(),
              "first trace emission should acquire and retain a services tracer instance");
  assert_equal(std::string("services"),
               provider->last_scope.name,
               "service trace bridge should request the frozen services tracer scope name");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "service trace bridge should preserve the frozen services tracer scope version");
  assert_true(provider->tracer != nullptr &&
                  provider->tracer->started_spans.size() == 1U,
              "starting one facade span should produce exactly one started trace record");

  const auto& record = provider->tracer->started_spans.front();
  const auto* stage = string_attr(record.descriptor.attrs, "services.stage");
  const auto* operation = string_attr(record.descriptor.attrs, "services.operation");
  const auto* profile = string_attr(record.descriptor.attrs, "services.profile");
  const auto* trace_ref = string_attr(record.descriptor.attrs, "services.trace_ref");

  assert_equal(std::string("services.facade.execute"),
               record.descriptor.name,
               "facade spans should use the frozen services.facade.<operation> naming contract");
  assert_true(record.descriptor.kind == SpanKind::Server,
              "facade spans should be emitted as server spans");
  assert_true(stage != nullptr && *stage == "facade" &&
                  operation != nullptr && *operation == "execute" &&
                  profile != nullptr && *profile == "edge_balanced" &&
                  trace_ref != nullptr && *trace_ref == "trace-026-tool",
              "facade spans should project frozen stage/operation/profile/request trace attributes");
  assert_true(record.explicit_parent.has_value() &&
                  record.explicit_parent->state == TraceContextState::Active &&
                  record.explicit_parent->is_remote &&
                  dasall::infra::tracing::is_lower_hex_string(
                      record.explicit_parent->trace_id,
                      dasall::infra::tracing::kTraceIdHexLength) &&
                  dasall::infra::tracing::is_lower_hex_string(
                      record.explicit_parent->span_id,
                      dasall::infra::tracing::kSpanIdHexLength),
              "facade span roots should synthesize a valid remote tool parent context from raw service call fields");
  assert_true(record.span->context.trace_id == record.explicit_parent->trace_id &&
                  record.span->context.parent_span_id == record.explicit_parent->span_id,
              "facade spans should inherit the remote tool trace_id and keep the remote span_id as parent_span_id");
  assert_true(record.span->status_code == SpanStatusCode::Ok &&
                  record.span->status_message.empty() &&
                  record.span->end_call_total == 1,
              "successful facade span completion should end the span once with terminal Ok status");

  const auto status = bridge.get_status();
  assert_true(status.has_consistent_state() && status.started_span_total == 1U &&
                  status.span_failure_total == 0U && !status.degraded &&
                  !status.last_error_code.has_value() &&
                  status.detail_ref == "trace://services/facade/execute/req-026",
              "successful facade tracing should keep the bridge healthy and retain the last completed detail_ref");
}

void test_service_trace_bridge_sets_from_cache_attribute_for_data_query_results() {
  auto provider = std::make_shared<RecordingTracerProvider>();
  ServiceTraceBridge bridge(provider,
                            ServiceTraceBridgeOptions{
                                .enabled = true,
                                .profile_id = "edge_balanced",
                                .trace_sample_ratio = 1.0,
                            });

  const auto target = make_target();
  auto span = bridge.start_lane_span(make_context(),
                                     "data.query",
                                     "status",
                                     &target);
  bridge.complete_span(&span,
                       DataQueryResult{
                 .code = std::nullopt,
                           .rows_json = "[{\"id\":1}]",
                           .from_cache = true,
                           .error = std::nullopt,
                       });

  const auto& record = provider->tracer->started_spans.front();
  const auto* from_cache = bool_attr(record.span->attributes,
                                     "services.from_cache");

  assert_true(from_cache != nullptr && *from_cache,
              "data query span completion should persist the from_cache fact as a span attribute");
  assert_true(record.span->status_code == SpanStatusCode::Ok &&
                  record.span->end_call_total == 1,
              "successful data query completion should end the lane span with Ok status");
  assert_true(!bridge.is_degraded(),
              "successful business-path spans should not move the trace bridge into degraded mode");
}

  void test_service_trace_bridge_marks_subscription_overflow_facts_on_completion() {
    auto provider = std::make_shared<RecordingTracerProvider>();
    ServiceTraceBridge bridge(provider,
                ServiceTraceBridgeOptions{
                  .enabled = true,
                  .profile_id = "edge_balanced",
                  .trace_sample_ratio = 1.0,
                });

    const auto target = make_target();
    auto span = bridge.start_lane_span(make_context(),
                     "execution.subscription_hub",
                     "subscribe",
                     &target);
    bridge.complete_span(&span,
               ExecutionSubscriptionResult{
                 .code = ResultCode::RuntimeRetryExhausted,
                 .events_json = "[{\"seq\":3}]",
                 .next_cursor = std::string("3"),
                 .resync_required = true,
                 .dropped_count = 2U,
                 .error = dasall::contracts::ErrorInfo{
                   .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
                   .retryable = true,
                   .safe_to_replan = false,
                   .details = {
                     .code = static_cast<int>(ResultCode::RuntimeRetryExhausted),
                     .message = "subscription overflow requires resync",
                     .stage = "execution_subscription_hub",
                   },
                   .source_ref = {
                     .ref_type = "subscription_stream",
                     .ref_id = "cap.exec:target-026:status",
                   },
                 },
               });

    const auto& record = provider->tracer->started_spans.front();
    const auto* resync_required = bool_attr(record.span->attributes,
                        "services.resync_required");
    const auto* dropped_count = int64_attr(record.span->attributes,
                       "services.dropped_count");
    const auto* next_cursor = string_attr(record.span->attributes,
                      "services.next_cursor");

    assert_true(resync_required != nullptr && *resync_required &&
            dropped_count != nullptr && *dropped_count == 2LL &&
            next_cursor != nullptr && *next_cursor == "3",
          "subscription completion should retain resync_required, dropped_count, and next_cursor as span attributes");
    assert_true(record.span->status_code == SpanStatusCode::Error &&
            record.span->status_message == "subscription overflow requires resync" &&
            record.span->end_call_total == 1,
          "overflow subscription results should terminate the span with an Error status that preserves the hub message");
    assert_true(!bridge.is_degraded(),
          "subscription business errors should not be mistaken for trace bridge degradation");
  }

void test_service_trace_bridge_provider_failure_does_not_block_primary_result() {
  ServiceTraceBridge bridge(nullptr,
                            ServiceTraceBridgeOptions{
                                .enabled = true,
                                .profile_id = "edge_balanced",
                                .trace_sample_ratio = 1.0,
                            });

  const auto target = make_target();
  auto span = bridge.start_lane_span(make_context(),
                                     "data.query",
                                     "status",
                                     &target);
  const auto result = bridge.with_span(span, []() {
    return std::string("primary-result");
  });

  assert_true(!span.is_valid(),
              "missing tracer providers should yield an invalid span handle instead of throwing");
  assert_equal(std::string("primary-result"),
               result,
               "trace bridge failures must not swallow or replace the primary business result");

  const auto status = bridge.get_status();
  assert_true(status.has_consistent_state() && status.degraded &&
                  status.started_span_total == 0U && status.span_failure_total == 1U &&
                  status.last_error_code.has_value() &&
                  *status.last_error_code == ResultCode::ProviderTimeout,
              "missing providers should degrade the trace bridge with a structured ProviderTimeout status");
  assert_equal(std::string("trace://services/provider_missing"),
               status.detail_ref,
               "missing-provider failures should retain a stable trace detail_ref for later health probing");
}

void test_service_trace_bridge_marks_adapter_transport_errors_without_degrading_bridge() {
  auto provider = std::make_shared<RecordingTracerProvider>();
  ServiceTraceBridge bridge(provider,
                            ServiceTraceBridgeOptions{
                                .enabled = true,
                                .profile_id = "edge_balanced",
                                .trace_sample_ratio = 1.0,
                            });

  auto span = bridge.start_external_span(make_selection(), make_adapter_request());
  bridge.complete_span(&span,
                       AdapterReceipt{
                           .receipt_ref = "receipt-026",
                           .adapter_id = "service-primary",
                           .route_kind = AdapterRouteKind::local_service,
                           .target_id = "target-026",
                           .transport_outcome = AdapterTransportOutcome::partial,
                           .provider_status_code = "partial_side_effect",
                           .payload_json = "{}",
                           .latency_ms = 17U,
                           .side_effects = {"safe_mode.enabled"},
                           .evidence_refs = {"audit://services/toggle"},
                       });

  const auto& record = provider->tracer->started_spans.front();
  const auto* latency_ms = int64_attr(record.span->attributes,
                                      "services.latency_ms");
  const auto* outcome = string_attr(record.span->attributes,
                                    "services.transport_outcome");

  assert_true(latency_ms != nullptr && *latency_ms == 17LL &&
                  outcome != nullptr && *outcome == "partial",
              "adapter/external span completion should retain latency and transport outcome facts as span attributes");
  assert_true(record.span->status_code == SpanStatusCode::Error &&
                  record.span->status_message == "partial_side_effect" &&
                  record.span->end_call_total == 1,
              "partial adapter receipts should terminate the span with an Error status that preserves provider status code context");

  const auto status = bridge.get_status();
  assert_true(status.has_consistent_state() && !status.degraded &&
                  status.span_failure_total == 0U &&
                  !status.last_error_code.has_value(),
              "business-path transport failures should not be mistaken for trace bridge degradation when the span itself ends successfully");
}

}  // namespace

int main() {
  try {
    test_service_trace_bridge_starts_facade_span_with_remote_parent_context();
    test_service_trace_bridge_sets_from_cache_attribute_for_data_query_results();
    test_service_trace_bridge_marks_subscription_overflow_facts_on_completion();
    test_service_trace_bridge_provider_failure_does_not_block_primary_result();
    test_service_trace_bridge_marks_adapter_transport_errors_without_degrading_bridge();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}