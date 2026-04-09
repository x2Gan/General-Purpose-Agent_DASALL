#include <exception>
#include <iostream>
#include <optional>

#include "execution/ExecutionQueryLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::ExecutionQueryRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterBridge;
using dasall::services::internal::AdapterBridgeDependencies;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouter;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::ExecutionQueryLane;
using dasall::services::internal::ExecutionQueryLaneDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServicePolicyView;

class QueryInvoker final : public IAdapterInvoker {
 public:
  explicit QueryInvoker(
      std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke)
      : invoke_(std::move(invoke)) {}

  [[nodiscard]] std::string_view adapter_id() const override { return "query-primary"; }
  [[nodiscard]] AdapterRouteKind route_kind() const override {
    return AdapterRouteKind::local_service;
  }
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override {
    return invoke_(request);
  }

 private:
  std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke_;
};

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-017",
      .session_id = "session-017",
      .trace_id = "trace-017",
      .tool_call_id = "tool-017",
      .goal_id = "goal-017",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] ExecutionQueryRequest make_request(
    ServiceDataFreshness freshness = ServiceDataFreshness::strict,
    std::string query_kind = "state") {
  return ExecutionQueryRequest{
      .context = make_context(),
      .target = {
          .capability_id = "cap.exec",
          .target_id = "target-017",
      },
      .query_kind = std::move(query_kind),
      .freshness = freshness,
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {},
      .supported_queries = {"state"},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  return ServicePolicyView{
      .local_platform_route_enabled = false,
      .adapter_preference_order = {AdapterRouteKind::local_service},
  };
}

[[nodiscard]] FallbackEnvelope make_fallback_envelope() {
  return FallbackEnvelope{
      .requested_action_class = "query.read_only",
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "query.read_only",
      .allow_degrade = true,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_candidate() {
  return AdapterCandidateView{
      .adapter_id = "query-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "query.read_only",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = {"cap.exec"},
  };
}

void test_execution_query_lane_returns_successful_read_only_snapshot() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest& request) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"power\":\"on\"}",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"snapshot://target-017/current"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = {},
      .extract_state = [](const auto&, const auto&) { return std::string("ready"); },
  });

  const auto result = lane.query_state(make_context(), make_request());

  assert_true(!result.error.has_value(), "successful query should not surface an error");
  assert_equal(std::string("ready"), result.state,
               "query lane should preserve extracted state value");
  assert_equal(std::string("{\"power\":\"on\"}"), result.snapshot_json,
               "query lane should return the adapter snapshot payload");
  assert_true(!result.from_cache, "successful live query should not be marked from_cache");
}

void test_execution_query_lane_rejects_invalid_request() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest&) { return AdapterInvocationResult{}; });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = {},
      .extract_state = {},
  });

  const auto result = lane.query_state(make_context(), make_request(ServiceDataFreshness::strict, ""));

  assert_true(result.error.has_value(), "invalid query should surface structured error info");
  assert_true(result.error->failure_type.has_value(),
              "invalid query should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "invalid query should map to validation failure type");
}

void test_execution_query_lane_surfaces_data_stale_for_strict_reads() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::rejected,
        .provider_status_code = "data_stale",
        .payload_json = "{\"snapshot_age_ms\":1200}",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"cache://snapshot/017"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = {},
      .extract_state = {},
  });

  const auto result = lane.query_state(make_context(), make_request(ServiceDataFreshness::strict));

  assert_true(result.error.has_value(), "strict stale query should surface structured runtime error");
  assert_true(result.error->failure_type.has_value(),
              "strict stale query should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Runtime),
               static_cast<int>(*result.error->failure_type),
               "strict stale query should map to runtime failure type");
  assert_true(!result.from_cache, "strict stale query must not pretend cached success");
}

void test_execution_query_lane_returns_cached_snapshot_when_allow_stale_is_enabled() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::rejected,
        .provider_status_code = "data_stale",
        .payload_json = "{\"snapshot_age_ms\":1200}",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"cache://snapshot/017"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = [](const auto&) {
        return std::optional<dasall::services::internal::CachedExecutionQuerySnapshot>{
            dasall::services::internal::CachedExecutionQuerySnapshot{
                .state = "cached_ready",
                .snapshot_json = "{\"power\":\"on\",\"cached\":true}",
                .snapshot_ref = "cache://snapshot/017",
            },
        };
      },
      .extract_state = {},
  });

  const auto result = lane.query_state(make_context(), make_request(ServiceDataFreshness::allow_stale));

  assert_true(!result.error.has_value(),
              "allow_stale query should fall back to cached snapshot when available");
  assert_true(result.from_cache, "allow_stale fallback should mark from_cache=true");
  assert_equal(std::string("cached_ready"), result.state,
               "cached query should preserve cached state value");
}

void test_execution_query_lane_surfaces_adapter_unavailable_errors() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::timeout,
        .provider_status_code = "adapter_unavailable",
        .payload_json = "{\"error\":\"timeout\"}",
        .latency_ms = 0U,
        .side_effects = {},
        .evidence_refs = {},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = {},
      .extract_state = {},
  });

  const auto result = lane.query_state(make_context(), make_request());

  assert_true(result.error.has_value(), "adapter timeout should surface an error");
  assert_true(result.error->failure_type.has_value(),
              "adapter timeout should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(*result.error->failure_type),
               "adapter timeout should map to provider failure type");
}

void test_execution_query_lane_rejects_query_receipts_with_side_effects() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const QueryInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"power\":\"on\"}",
        .latency_ms = 4U,
        .side_effects = {"switch.enabled"},
        .evidence_refs = {"audit://query/side-effect-017"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  const ExecutionQueryLane lane(ExecutionQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .load_cached_snapshot = {},
      .extract_state = {},
  });

  const auto result = lane.query_state(make_context(), make_request());

  assert_true(result.error.has_value(),
              "query receipts with side effects should fail closed");
  assert_true(result.error->failure_type.has_value(),
              "read-only violation should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "read-only violation should map to validation failure type");
}

}  // namespace

int main() {
  try {
    test_execution_query_lane_returns_successful_read_only_snapshot();
    test_execution_query_lane_rejects_invalid_request();
    test_execution_query_lane_surfaces_data_stale_for_strict_reads();
    test_execution_query_lane_returns_cached_snapshot_when_allow_stale_is_enabled();
    test_execution_query_lane_surfaces_adapter_unavailable_errors();
    test_execution_query_lane_rejects_query_receipts_with_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}