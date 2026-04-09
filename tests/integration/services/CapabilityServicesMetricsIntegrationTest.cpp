#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ServiceFacade.h"
#include "bridges/ServiceMetricsBridge.h"
#include "data/DataProjectionCache.h"
#include "data/DataQueryLane.h"
#include "execution/ExecutionCommandLane.h"
#include "execution/ExecutionQueryLane.h"
#include "execution/ExecutionSubscriptionHub.h"
#include "metrics/MetricTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::CapabilityTargetRef;
using dasall::services::DataQueryRequest;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ExecutionQueryRequest;
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
using dasall::services::internal::ExecutionQueryLane;
using dasall::services::internal::ExecutionQueryLaneDependencies;
using dasall::services::internal::ExecutionSubscriptionHub;
using dasall::services::internal::ExecutionSubscriptionHubDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServiceContextBuilder;
using dasall::services::internal::ServiceFacade;
using dasall::services::internal::ServiceFacadeDependencies;
using dasall::services::internal::ServiceMetricsBridge;
using dasall::services::internal::ServiceMetricsBridgeOptions;
using dasall::services::internal::ServicePolicyView;

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
        "metrics://services/integration-recorded");
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
        "metrics://services/integration-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/integration-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/integration-provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<RecordingMeter> meter_;
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
          .transport_outcome = AdapterTransportOutcome::partial,
          .provider_status_code = "partial_side_effect",
          .payload_json = request.payload_json,
          .latency_ms = 7U,
          .side_effects = {"safe_mode.enabled"},
          .evidence_refs = {std::string("audit://services/") + request.operation_name},
      };
    }

    if (request.capability_id == "cap.exec") {
      return AdapterInvocationResult{
          .transport_outcome = AdapterTransportOutcome::acknowledged,
          .provider_status_code = "ok",
          .payload_json = "{\"state\":\"ready\"}",
          .latency_ms = 5U,
          .side_effects = {},
          .evidence_refs = {"state://cap.exec/ready"},
      };
    }

    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "[{\"id\":1,\"state\":\"ready\"}]",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"cache://devices/status/live"},
    };
  }
};

[[nodiscard]] ServiceCallContext make_context(std::string request_id) {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000U;

  return ServiceCallContext{
      .request_id = std::move(request_id),
      .session_id = "session-025-int",
      .trace_id = "trace-025-int",
      .tool_call_id = "tool-call-025-int",
      .goal_id = "goal-025-int",
      .budget_guard = budget,
      .deadline_ms = 12000U,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  ServicePolicyView policy_view{};
  policy_view.effective_profile_id = "edge_balanced";
  policy_view.metrics_granularity = "full";
  policy_view.observability_bridge_enabled = true;
  policy_view.adapter_preference_order = {AdapterRouteKind::local_service};
  return policy_view;
}

[[nodiscard]] CapabilitySnapshotView make_execution_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"safe_mode.enter"},
      .supported_queries = {"state"},
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

[[nodiscard]] bool has_sample(const std::vector<dasall::infra::metrics::MetricSample>& samples,
                              const std::string& name,
                              const std::string& stage) {
  return std::any_of(samples.begin(), samples.end(), [&](const auto& sample) {
    return sample.identity_ref.name == name && sample.labels.stage == stage;
  });
}

void test_capability_services_metrics_integration_observes_command_query_cache_and_overflow() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  ServiceMetricsBridge metrics_bridge(provider,
                                      ServiceMetricsBridgeOptions{
                                          .enabled = true,
                                          .profile_id = "edge_balanced",
                                          .metrics_granularity = "full",
                                          .meter_scope_name = "services",
                                          .meter_scope_version = "v1",
                                          .now_ms = []() { return 1712736010000LL; },
                                      });

  const ScriptedInvoker invoker;
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 5000U,
      .now_ms = []() { return 1712736010000ULL; },
  });

  ExecutionCommandLane command_lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .compensation_catalog = nullptr,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_execution_snapshot(),
      .fallback_envelope = make_fallback_envelope("command.high_risk"),
      .registered_candidates = {make_candidate({"cap.exec", "devices"})},
      .critical_actions = {"safe_mode.enter"},
      .high_risk_actions = {},
      .allow_high_risk_actions = true,
      .lookup_compensation_hints = [](const std::string&,
                                      const std::string&,
                                      const std::string&,
                                      const dasall::services::internal::AdapterReceipt&) {
        return std::vector<std::string>{"safe_mode.exit"};
      },
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
      .audit_bridge = nullptr,
      .metrics_bridge = &metrics_bridge,
  });

  ExecutionQueryLane query_lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_execution_snapshot(),
      .fallback_envelope = make_fallback_envelope("query.read_only"),
      .registered_candidates = {make_candidate({"cap.exec", "devices"})},
      .load_cached_snapshot = {},
      .extract_state = [](const dasall::services::internal::AdapterReceipt&, const ExecutionQueryRequest&) {
        return std::string("ready");
      },
      .metrics_bridge = &metrics_bridge,
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
      .metrics_bridge = &metrics_bridge,
  });

  ExecutionSubscriptionHub subscription_hub(
      ExecutionSubscriptionHubDependencies{
          .max_buffered_events = 1U,
          .metrics_bridge = &metrics_bridge,
      });

  ServiceContextBuilder context_builder;
  ServiceFacade facade(ServiceFacadeDependencies{
      .context_builder = &context_builder,
      .execute_command = [&](const ServiceCallContext& context,
                             const ExecutionCommandRequest& request) {
        return command_lane.execute(context, request);
      },
      .compensate_command = {},
      .query_execution_state = [&](const ServiceCallContext& context,
                                   const ExecutionQueryRequest& request) {
        return query_lane.query_state(context, request);
      },
      .subscribe_execution_state = [&](const ServiceCallContext& context,
                                       const ExecutionSubscriptionRequest& request) {
        return subscription_hub.subscribe(context, request);
      },
      .diagnose_execution_target = {},
      .query_data = [&](const ServiceCallContext& context,
                        const DataQueryRequest& request) {
        return data_lane.query(context, request);
      },
      .list_data_capabilities = {},
  });

  const auto execute_result = facade.execute(ExecutionCommandRequest{
      .context = make_context("req-025-exec"),
      .target = CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-025"},
      .action = "safe_mode.enter",
      .arguments_json = "{}",
      .idempotency_key = std::string("idem-025"),
  });
  const auto query_result = facade.query_state(ExecutionQueryRequest{
      .context = make_context("req-025-query"),
      .target = CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-025"},
      .query_kind = "state",
      .freshness = ServiceDataFreshness::strict,
  });
  const auto data_live_result = facade.query(DataQueryRequest{
      .context = make_context("req-025-data-live"),
      .dataset = "devices",
      .filters_json = "{\"region\":\"lab\"}",
      .projection = "status",
      .freshness = ServiceDataFreshness::strict,
  });
  const auto data_cache_result = facade.query(DataQueryRequest{
      .context = make_context("req-025-data-cache"),
      .dataset = "devices",
      .filters_json = "{\"region\":\"lab\"}",
      .projection = "status",
      .freshness = ServiceDataFreshness::strict,
  });

  subscription_hub.publish(CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-025"},
                           "status",
                           {"{\"seq\":1}", "{\"seq\":2}"});
  const auto subscription_result = facade.subscribe(ExecutionSubscriptionRequest{
      .context = make_context("req-025-subscribe"),
      .target = CapabilityTargetRef{.capability_id = "cap.exec", .target_id = "target-025"},
      .stream_kind = "status",
      .cursor = std::string("0"),
      .max_events = 2U,
  });

  assert_true(execute_result.error.has_value() &&
                  execute_result.compensation_hints.size() == 1U,
              "metrics integration should preserve partial-side-effect command facts while observability is enabled");
  assert_true(!query_result.error.has_value(),
              "metrics integration should keep execution query results intact");
  assert_true(!data_live_result.error.has_value() && !data_live_result.from_cache,
              "first data query should come from the live adapter path");
  assert_true(!data_cache_result.error.has_value() && data_cache_result.from_cache,
              "second data query should be served from cache to expose cache-hit metrics");
  assert_true(subscription_result.resync_required && subscription_result.dropped_count == 1U,
              "subscription overflow should still surface resync_required and dropped_count while metrics are enabled");

  assert_true(metrics_bridge.has_active_meter() && metrics_bridge.instruments_registered(),
              "integration flow should acquire the services meter and register instruments once through the metrics bridge");
  assert_equal(std::string("services"),
               provider->last_scope.name,
               "services metrics integration should request the frozen services meter scope");
  assert_equal(6,
               static_cast<int>(meter->created_identities.size()),
               "services metrics integration should register the six frozen services metric families once");
  assert_true(has_sample(meter->recorded_samples,
                         "services_execution_requests_total",
                         "execution.command.safe_mode.enter") &&
                  has_sample(meter->recorded_samples,
                             "services_execution_latency_ms",
                             "execution.adapter.service-primary.safe_mode.enter") &&
                  has_sample(meter->recorded_samples,
                             "services_compensation_hint_total",
                             "execution.compensation.safe_mode.enter") &&
                  has_sample(meter->recorded_samples,
                             "services_execution_requests_total",
                             "execution.query.state.cache_miss") &&
                  has_sample(meter->recorded_samples,
                             "services_data_query_requests_total",
                             "data.query.status.cache_miss") &&
                  has_sample(meter->recorded_samples,
                             "services_data_query_requests_total",
                             "data.query.status.cache_hit") &&
                  has_sample(meter->recorded_samples,
                             "services_subscription_overflow_total",
                             "subscription.status.cap.exec"),
              "services metrics integration should make command/query/cache/overflow metric emissions observable through the shared meter");
  assert_true(!metrics_bridge.is_degraded(),
              "successful integration emissions should keep the service metrics bridge healthy");
  assert_true(metrics_bridge.emission_attempt_total() >= 9U,
              "integration flow should record multiple metric samples across command, query, cache hit, and subscription overflow paths");
}

}  // namespace

int main() {
  try {
    test_capability_services_metrics_integration_observes_command_query_cache_and_overflow();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}