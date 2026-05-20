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

#include "ServiceFacade.h"
#include "bridges/ServiceTraceBridge.h"
#include "data/DataProjectionCache.h"
#include "data/DataQueryLane.h"
#include "execution/ExecutionCommandLane.h"
#include "execution/ExecutionSubscriptionHub.h"
#include "mapping/ResultMapper.h"
#include "support/TestAssertions.h"

namespace {

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
using dasall::services::DataQueryRequest;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ExecutionSubscriptionRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterBridge;
using dasall::services::internal::AdapterBridgeDependencies;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterRouter;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::DataProjectionCache;
using dasall::services::internal::DataProjectionCacheDependencies;
using dasall::services::internal::DataQueryLane;
using dasall::services::internal::DataQueryLaneDependencies;
using dasall::services::internal::ExecutionCommandLane;
using dasall::services::internal::ExecutionCommandLaneDependencies;
using dasall::services::internal::ExecutionSubscriptionHub;
using dasall::services::internal::ExecutionSubscriptionHubDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServiceContextBuilder;
using dasall::services::internal::ServiceFacade;
using dasall::services::internal::ServiceFacadeDependencies;
using dasall::services::internal::ServicePolicyView;
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

  void add_event(std::string_view,
                 const TraceAttributeMap&) override {}

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
                               : std::optional<std::int64_t>(1712736030000LL),
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
};

class ScriptedInvoker final : public IAdapterInvoker {
 public:
  [[nodiscard]] std::string_view adapter_id() const override {
    return "service-primary";
  }

  [[nodiscard]] AdapterRouteKind route_kind() const override {
    return AdapterRouteKind::local_service;
  }

  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override {
    if (request.request_kind == AdapterRouteRequestKind::action) {
      return AdapterInvocationResult{
          .transport_outcome = AdapterTransportOutcome::acknowledged,
          .provider_status_code = "ok",
          .payload_json = "{\"status\":\"ok\"}",
          .latency_ms = 7U,
          .side_effects = {},
          .evidence_refs = {"audit://services/toggle"},
      };
    }

    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "[{\"id\":1,\"state\":\"ready\"}]",
        .latency_ms = 5U,
        .side_effects = {},
        .evidence_refs = {"cache://devices/status/live"},
    };
  }
};

[[nodiscard]] ServiceCallContext make_context(std::string request_id,
                                              std::string trace_id,
                                              std::string tool_call_id) {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000U;

  return ServiceCallContext{
      .request_id = std::move(request_id),
      .session_id = "session-026-int",
      .trace_id = std::move(trace_id),
      .tool_call_id = std::move(tool_call_id),
      .goal_id = "goal-026-int",
      .budget_guard = budget,
      .deadline_ms = 12000U,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  ServicePolicyView policy_view{};
  policy_view.effective_profile_id = "edge_balanced";
  policy_view.observability_bridge_enabled = true;
  policy_view.adapter_preference_order = {AdapterRouteKind::local_service};
  return policy_view;
}

[[nodiscard]] CapabilitySnapshotView make_execution_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"toggle"},
      .supported_queries = {},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] CapabilitySnapshotView make_data_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "devices",
      .capability_version = "v1",
      .supported_actions = {},
      .supported_queries = {"status", "catalog.list"},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] FallbackEnvelope make_fallback_envelope(std::string action_class) {
  return FallbackEnvelope{
      .requested_action_class = std::move(action_class),
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "service.local",
      .allow_degrade = true,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_candidate(std::vector<std::string> capabilities) {
  return AdapterCandidateView{
      .adapter_id = "service-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "service.local",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = std::move(capabilities),
  };
}

[[nodiscard]] const std::string* string_attr(const TraceAttributeMap& attrs,
                                             std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<std::string>(attr) : nullptr;
}

void assert_chain(const StartedSpanRecord& root,
                  const StartedSpanRecord& lane,
                  const StartedSpanRecord& adapter,
                  const StartedSpanRecord& external,
                  std::string root_name,
                  std::string lane_name,
                  std::string adapter_name,
                  std::string external_name) {
  assert_equal(root_name,
               root.descriptor.name,
               "trace integration should start the expected facade span name");
  assert_equal(lane_name,
               lane.descriptor.name,
               "trace integration should start the expected lane span name");
  assert_equal(adapter_name,
               adapter.descriptor.name,
               "trace integration should start the expected adapter span name");
  assert_equal(external_name,
               external.descriptor.name,
               "trace integration should start the expected external span name");

  assert_true(root.descriptor.kind == SpanKind::Server &&
                  lane.descriptor.kind == SpanKind::Internal &&
                  adapter.descriptor.kind == SpanKind::Client &&
                  external.descriptor.kind == SpanKind::Client,
              "trace integration should preserve server/internal/client/client span kinds across the full services chain");
  assert_true(root.explicit_parent.has_value() && root.explicit_parent->is_remote,
              "facade root spans should originate from a synthesized remote tool parent context");
  assert_true(lane.resolved_parent.has_value() &&
                  lane.resolved_parent->span_id == root.span->context.span_id &&
                  adapter.resolved_parent.has_value() &&
                  adapter.resolved_parent->span_id == lane.span->context.span_id &&
                  external.resolved_parent.has_value() &&
                  external.resolved_parent->span_id == adapter.span->context.span_id,
              "lane, adapter, and external spans should nest strictly under the previously active services span");
  assert_true(root.span->context.trace_id == lane.span->context.trace_id &&
                  lane.span->context.trace_id == adapter.span->context.trace_id &&
                  adapter.span->context.trace_id == external.span->context.trace_id,
              "all spans in one services request chain should share the same trace_id");
  assert_true(root.span->status_code == SpanStatusCode::Ok &&
                  lane.span->status_code == SpanStatusCode::Ok &&
                  adapter.span->status_code == SpanStatusCode::Ok &&
                  external.span->status_code == SpanStatusCode::Ok,
              "successful services integration flows should terminate all spans with Ok status");
  assert_true(root.span->end_call_total == 1 && lane.span->end_call_total == 1 &&
                  adapter.span->end_call_total == 1 && external.span->end_call_total == 1,
              "each services trace span should end exactly once during the integration flow");
}

void test_capability_services_trace_integration_wires_facade_lane_adapter_and_external_chain() {
  auto provider = std::make_shared<RecordingTracerProvider>();
  ServiceTraceBridge trace_bridge(provider,
                                  ServiceTraceBridgeOptions{
                                      .enabled = true,
                                      .profile_id = "edge_balanced",
                                      .trace_sample_ratio = 1.0,
                                  });

  const ScriptedInvoker invoker;
  const AdapterBridge bridge(AdapterBridgeDependencies{
      .invokers = {&invoker},
      .trace_bridge = &trace_bridge,
  });
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 5000U,
      .now_ms = []() { return 1712736030000ULL; },
  });

  ExecutionCommandLane command_lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .compensation_catalog = nullptr,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_execution_snapshot(),
      .fallback_envelope = make_fallback_envelope("command.standard"),
      .registered_candidates = {make_candidate({"cap.exec", "devices"})},
      .critical_actions = {},
      .high_risk_actions = {},
      .allow_high_risk_actions = true,
      .lookup_compensation_hints = {},
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
      .audit_bridge = nullptr,
      .metrics_bridge = nullptr,
      .trace_bridge = &trace_bridge,
  });

  DataQueryLane data_lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_data_snapshot(),
      .fallback_envelope = make_fallback_envelope("query.read_only"),
      .registered_candidates = {make_candidate({"cap.exec", "devices"})},
      .metrics_bridge = nullptr,
      .trace_bridge = &trace_bridge,
  });

  ServiceContextBuilder context_builder;
  ServiceFacade facade(ServiceFacadeDependencies{
      .context_builder = &context_builder,
      .execute_command = [&](const ServiceCallContext& context,
                             const ExecutionCommandRequest& request) {
        return command_lane.execute(context, request);
      },
      .compensate_command = {},
      .query_execution_state = {},
      .subscribe_execution_state = {},
      .diagnose_execution_target = {},
      .query_data = [&](const ServiceCallContext& context,
                        const DataQueryRequest& request) {
        return data_lane.query(context, request);
      },
      .list_data_capabilities = {},
      .trace_bridge = &trace_bridge,
  });

  const auto execute_result = facade.execute(ExecutionCommandRequest{
      .context = make_context("req-026-exec", "trace-026-exec", "tool-call-026-exec"),
      .target = CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-026"},
      .action = "toggle",
      .arguments_json = "{}",
      .idempotency_key = std::nullopt,
  });
  const auto data_result = facade.query(DataQueryRequest{
      .context = make_context("req-026-data", "trace-026-data", "tool-call-026-data"),
      .dataset = "devices",
      .filters_json = "{\"region\":\"lab\"}",
      .projection = "status",
      .freshness = ServiceDataFreshness::strict,
  });

  assert_true(!execute_result.error.has_value() &&
                  execute_result.execution_id == "exec:req-026-exec:toggle",
              "trace integration should keep command execution results intact while tracing is enabled");
  assert_true(!data_result.error.has_value() && !data_result.from_cache,
              "trace integration should keep live data query results intact while tracing is enabled");
  assert_true(trace_bridge.has_active_tracer(),
              "integration flow should acquire a shared services tracer instance");
  assert_equal(std::string("services"),
               provider->last_scope.name,
               "trace integration should request the frozen services tracer scope name");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "trace integration should preserve the frozen services tracer scope version");
  assert_true(provider->tracer != nullptr &&
                  provider->tracer->started_spans.size() == 8U,
              "command plus data-query flows should start exactly eight spans across facade, lane, adapter, and external layers");

  const auto& execute_root = provider->tracer->started_spans[0];
  const auto& execute_lane = provider->tracer->started_spans[1];
  const auto& execute_adapter = provider->tracer->started_spans[2];
  const auto& execute_external = provider->tracer->started_spans[3];
  const auto& data_root = provider->tracer->started_spans[4];
  const auto& data_lane_span = provider->tracer->started_spans[5];
  const auto& data_adapter = provider->tracer->started_spans[6];
  const auto& data_external = provider->tracer->started_spans[7];

  assert_chain(execute_root,
               execute_lane,
               execute_adapter,
               execute_external,
               "services.facade.execute",
               "services.lane.execution.command.toggle",
               "services.adapter.local_service.service-primary",
               "services.external.cap.exec.toggle");
  assert_chain(data_root,
               data_lane_span,
               data_adapter,
               data_external,
               "services.facade.query",
               "services.lane.data.query.status",
               "services.adapter.local_service.service-primary",
               "services.external.devices.status");

  const auto* execute_stage = string_attr(execute_lane.descriptor.attrs,
                                          "services.stage");
  const auto* execute_capability = string_attr(execute_lane.descriptor.attrs,
                                               "services.capability_id");
  const auto* data_stage = string_attr(data_lane_span.descriptor.attrs,
                                       "services.stage");
  const auto* data_target = string_attr(data_lane_span.descriptor.attrs,
                                        "services.target_id");

  assert_true(execute_stage != nullptr && *execute_stage == "lane" &&
                  execute_capability != nullptr && *execute_capability == "cap.exec" &&
                  data_stage != nullptr && *data_stage == "lane" &&
                  data_target != nullptr && *data_target == "devices",
              "trace integration should preserve frozen lane attributes for execution and data query spans");
  assert_true(execute_root.span->context.trace_id != data_root.span->context.trace_id,
              "distinct service requests should keep isolated trace roots even when they share one tracer instance");

  const auto status = trace_bridge.get_status();
  assert_true(status.has_consistent_state() && !status.degraded &&
                  status.started_span_total == 8U &&
                  status.span_failure_total == 0U,
              "successful integration tracing should leave the bridge healthy while accounting for every started span");
}

  void test_capability_services_trace_integration_wires_subscription_facade_and_hub_chain() {
    auto provider = std::make_shared<RecordingTracerProvider>();
    ServiceTraceBridge trace_bridge(provider,
                    ServiceTraceBridgeOptions{
                      .enabled = true,
                      .profile_id = "edge_balanced",
                      .trace_sample_ratio = 1.0,
                    });

    ExecutionSubscriptionHub subscription_hub(ExecutionSubscriptionHubDependencies{
      .max_buffered_events = 4U,
      .metrics_bridge = nullptr,
      .trace_bridge = &trace_bridge,
    });
    subscription_hub.publish(CapabilityTargetRef{
                   .capability_id = "cap.exec",
                   .target_id = "target-026-sub",
                 },
                 "status",
                 {"{\"seq\":1}", "{\"seq\":2}"});

    ServiceContextBuilder context_builder;
    ServiceFacade facade(ServiceFacadeDependencies{
      .context_builder = &context_builder,
      .execute_command = {},
      .compensate_command = {},
      .query_execution_state = {},
      .subscribe_execution_state = [&](const ServiceCallContext& context,
                       const ExecutionSubscriptionRequest& request) {
      return subscription_hub.subscribe(context, request);
      },
      .diagnose_execution_target = {},
      .query_data = {},
      .list_data_capabilities = {},
      .trace_bridge = &trace_bridge,
    });

    const auto result = facade.subscribe(ExecutionSubscriptionRequest{
      .context = make_context("req-026-sub", "trace-026-sub", "tool-call-026-sub"),
      .target = CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-026-sub"},
      .stream_kind = "status",
      .cursor = std::string("0"),
      .max_events = 2U,
    });

    assert_true(!result.error.has_value() && result.next_cursor.has_value() &&
            *result.next_cursor == "2" &&
            result.events_json == "[{\"seq\":1},{\"seq\":2}]",
          "subscription trace integration should preserve the public subscribe result while tracing is enabled");
    assert_true(trace_bridge.has_active_tracer(),
          "subscription tracing should acquire a shared services tracer instance");
    assert_true(provider->tracer != nullptr &&
            provider->tracer->started_spans.size() == 2U,
          "subscription tracing should start exactly facade and hub lane spans");

    const auto& root = provider->tracer->started_spans[0];
    const auto& lane = provider->tracer->started_spans[1];
    const auto* lane_stage = string_attr(lane.descriptor.attrs, "services.stage");
    const auto* lane_capability = string_attr(lane.descriptor.attrs,
                        "services.capability_id");
    const auto* lane_target = string_attr(lane.descriptor.attrs, "services.target_id");
    const auto* stream_kind = string_attr(lane.span->attributes, "services.stream_kind");

    assert_equal(std::string("services.facade.subscribe"),
           root.descriptor.name,
           "subscription tracing should start the frozen facade subscribe span");
    assert_equal(std::string("services.lane.execution.subscription_hub.subscribe"),
           lane.descriptor.name,
           "subscription tracing should start the frozen hub lane span");
    assert_true(root.descriptor.kind == SpanKind::Server &&
            lane.descriptor.kind == SpanKind::Internal,
          "subscription tracing should preserve server/internal kinds across facade and hub spans");
    assert_true(root.explicit_parent.has_value() && root.explicit_parent->is_remote,
          "subscription facade spans should synthesize a remote tool parent context");
    assert_true(lane.resolved_parent.has_value() &&
            lane.resolved_parent->span_id == root.span->context.span_id &&
            root.span->context.trace_id == lane.span->context.trace_id,
          "subscription hub spans should nest directly under the facade span in the same trace");
    assert_true(lane_stage != nullptr && *lane_stage == "lane" &&
            lane_capability != nullptr && *lane_capability == "cap.exec" &&
            lane_target != nullptr && *lane_target == "target-026-sub" &&
            stream_kind != nullptr && *stream_kind == "status",
          "subscription hub spans should retain frozen lane and stream_kind attributes");
    assert_true(root.span->status_code == SpanStatusCode::Ok &&
            lane.span->status_code == SpanStatusCode::Ok &&
            root.span->end_call_total == 1 && lane.span->end_call_total == 1,
          "successful subscription tracing should end facade and hub spans exactly once with Ok status");

    const auto status = trace_bridge.get_status();
    assert_true(status.has_consistent_state() && !status.degraded &&
            status.started_span_total == 2U &&
            status.span_failure_total == 0U,
          "successful subscription tracing should keep the bridge healthy while accounting for both spans");
  }

}  // namespace

int main() {
  try {
    test_capability_services_trace_integration_wires_facade_lane_adapter_and_external_chain();
    test_capability_services_trace_integration_wires_subscription_facade_and_hub_chain();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}