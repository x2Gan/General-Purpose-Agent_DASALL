#include <functional>
#include <exception>
#include <iostream>

#include "data/DataProjectionCache.h"
#include "data/DataQueryLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::DataCatalogRequest;
using dasall::services::DataQueryRequest;
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
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServicePolicyView;

class DataInvoker final : public IAdapterInvoker {
 public:
  explicit DataInvoker(
      std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke)
      : invoke_(std::move(invoke)) {}

  [[nodiscard]] std::string_view adapter_id() const override { return "data-primary"; }
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
      .request_id = "req-021",
      .session_id = "session-021",
      .trace_id = "trace-021",
      .tool_call_id = "tool-021",
      .goal_id = "goal-021",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] DataQueryRequest make_query_request(
    ServiceDataFreshness freshness = ServiceDataFreshness::strict,
    std::string projection = "status",
    std::string dataset = "devices") {
  return DataQueryRequest{
      .context = make_context(),
      .dataset = std::move(dataset),
      .filters_json = "{\"region\":\"lab\"}",
      .projection = std::move(projection),
      .freshness = freshness,
  };
}

[[nodiscard]] DataCatalogRequest make_catalog_request() {
  return DataCatalogRequest{
      .context = make_context(),
      .target_class = "devices",
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot(
    std::string capability_id = "devices",
    std::vector<std::string> supported_queries = {"status", "catalog.list"}) {
  return CapabilitySnapshotView{
      .capability_id = std::move(capability_id),
      .capability_version = "v1",
      .supported_actions = {},
      .supported_queries = std::move(supported_queries),
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  ServicePolicyView policy_view{};
  policy_view.local_platform_route_enabled = false;
  policy_view.adapter_preference_order = {AdapterRouteKind::local_service};
  return policy_view;
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

[[nodiscard]] AdapterCandidateView make_candidate(
    std::vector<std::string> supported_capabilities = {"devices"}) {
  return AdapterCandidateView{
      .adapter_id = "data-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "query.read_only",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = std::move(supported_capabilities),
  };
}

void test_data_query_lane_populates_cache_on_miss_and_reuses_hit() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  int invoke_count = 0;
  bool saw_projection = false;
  bool saw_filters = false;
  const DataInvoker invoker([&](const AdapterInvocationRequest& request) {
    ++invoke_count;
    saw_projection = request.request_kind == AdapterRouteRequestKind::query &&
                     request.operation_name == "status";
    saw_filters = request.payload_json.find("\"region\":\"lab\"") != std::string::npos;
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "[{\"id\":1,\"state\":\"ready\"}]",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {"cache://devices/status/live"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });

  DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto request = make_query_request();
  const auto first = lane.query(make_context(), request);
  now_ms = 1200U;
  const auto second = lane.query(make_context(), request);

  assert_true(saw_projection, "data query should forward projection as operation name");
  assert_true(saw_filters, "data query should forward filters_json into adapter payload");
  assert_equal(1,
               invoke_count,
               "cache hit should reuse stored snapshot without reinvoking adapter");
  assert_true(first.has_consistent_values() && first.succeeded(),
              "live data query should keep a successful result triad");
  assert_true(!first.error.has_value(), "cache miss live query should succeed");
  assert_true(!first.from_cache, "first query should come from live adapter path");
  assert_true(second.from_cache, "second query should be served from cache");
  assert_true(second.has_consistent_values() && second.succeeded(),
              "cache hit should keep a success triad instead of surfacing a synthetic failure code");
}

void test_data_query_lane_returns_data_stale_for_strict_stale_cache() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  int invoke_count = 0;
  const DataInvoker invoker([&](const AdapterInvocationRequest&) {
    ++invoke_count;
    return AdapterInvocationResult{};
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 100U,
      .now_ms = [&]() { return now_ms; },
  });
  const auto request = make_query_request(ServiceDataFreshness::strict);
  cache.store(request, "[{\"id\":1}]", "cache://devices/stale");
  now_ms = 1205U;

  DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = lane.query(make_context(), request);

  assert_equal(0,
               invoke_count,
               "strict stale cache should fail fast without live adapter invocation");
  assert_true(result.error.has_value(), "strict stale cache should surface an error");
  assert_true(!result.from_cache, "strict stale cache must not set from_cache");
  assert_true(result.error->failure_type.has_value(),
              "strict stale cache should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Runtime),
               static_cast<int>(*result.error->failure_type),
               "strict stale cache should map to runtime failure type");
}

void test_data_query_lane_allows_stale_cache_reads_when_requested() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  int invoke_count = 0;
  const DataInvoker invoker([&](const AdapterInvocationRequest&) {
    ++invoke_count;
    return AdapterInvocationResult{};
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 100U,
      .now_ms = [&]() { return now_ms; },
  });
  const auto request = make_query_request(ServiceDataFreshness::allow_stale);
  cache.store(request, "[{\"id\":2,\"cached\":true}]", "cache://devices/allow-stale");
  now_ms = 1300U;

  DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = lane.query(make_context(), request);

  assert_equal(0,
               invoke_count,
               "allow_stale cache should not invoke live adapter when snapshot exists");
  assert_true(result.has_consistent_values() && result.succeeded(),
              "allow_stale cache hit should preserve a successful result triad");
  assert_true(!result.error.has_value(), "allow_stale cache should still succeed");
  assert_true(result.from_cache, "allow_stale cache should mark from_cache=true");
  assert_equal(std::string("[{\"id\":2,\"cached\":true}]"),
               result.rows_json,
               "allow_stale cache should return cached rows payload");
}

void test_data_query_lane_rejects_query_receipts_with_side_effects() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  const DataInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "[{\"id\":1}]",
        .latency_ms = 3U,
        .side_effects = {"mutated"},
        .evidence_refs = {"audit://data-side-effect"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });

  DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = lane.query(make_context(), make_query_request());

  assert_true(result.error.has_value(), "query receipt with side_effects should fail closed");
  assert_true(result.error->failure_type.has_value(),
              "side_effect violation should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "query side_effect violation should map to validation failure type");
}

void test_data_query_lane_lists_capabilities_without_using_cache() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  bool saw_catalog_operation = false;
  const DataInvoker invoker([&](const AdapterInvocationRequest& request) {
    saw_catalog_operation = request.operation_name == "catalog.list" &&
                            request.payload_json == "{\"target_class\":\"devices\"}";
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"datasets\":[\"devices\",\"alerts\"]}",
        .latency_ms = 2U,
        .side_effects = {},
        .evidence_refs = {"catalog://devices"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });

  const DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
  });

  const auto result = lane.list_capabilities(make_context(), make_catalog_request());

  assert_true(saw_catalog_operation,
              "list_capabilities should invoke catalog.list with target_class payload");
  assert_true(!result.error.has_value(), "catalog listing should not surface an error");
  assert_equal(std::string("{\"datasets\":[\"devices\",\"alerts\"]}"),
               result.catalog_json,
               "catalog listing should return adapter catalog payload");
}

void test_data_query_lane_resolves_hot_updated_route_views_without_rebuilding_lane() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  int invoke_count = 0;
  std::string last_dataset;
  const DataInvoker invoker([&](const AdapterInvocationRequest& request) {
    ++invoke_count;
    last_dataset = request.capability_id;
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("[{\"dataset\":\"") + request.capability_id +
                        "\",\"projection\":\"" + request.operation_name + "\"}]",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {std::string("cache://") + request.capability_id + "/" +
                          request.operation_name},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });

  CapabilitySnapshotView current_snapshot = make_snapshot("devices", {"status", "catalog.list"});
  std::vector<AdapterCandidateView> current_candidates = {make_candidate({"devices"})};

  DataQueryLane lane(DataQueryLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .projection_cache = &cache,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot("stale.static", {"default", "catalog.list"}),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {},
      .resolve_route_view = [&](const std::string& capability_id,
                                AdapterRouteRequestKind request_kind) {
        assert_equal(static_cast<int>(AdapterRouteRequestKind::query),
                     static_cast<int>(request_kind),
                     "data lane should resolve a query route view for each request");
        assert_equal(current_snapshot.capability_id,
                     capability_id,
                     "route-view provider should receive the requested dataset");
        return dasall::services::internal::CapabilityRouteView{
            .capability_snapshot = current_snapshot,
            .registered_candidates = current_candidates,
        };
      },
  });

  const auto devices_result = lane.query(
      make_context(),
      make_query_request(ServiceDataFreshness::strict, "status", "devices"));

  current_snapshot = make_snapshot("alerts", {"summary", "catalog.list"});
  current_candidates = {make_candidate({"alerts"})};
  now_ms = 1200U;

  const auto alerts_result = lane.query(
      make_context(),
      make_query_request(ServiceDataFreshness::strict, "summary", "alerts"));

  assert_equal(2,
               invoke_count,
               "dynamic route views should allow new datasets without reconstructing the lane");
  assert_equal(std::string("alerts"),
               last_dataset,
               "hot-updated dataset should reach the adapter through the resolved route view");
  assert_true(devices_result.succeeded() && !devices_result.error.has_value(),
              "first request should succeed through the provider-backed dataset route view");
  assert_true(alerts_result.succeeded() && !alerts_result.error.has_value(),
              "second request should succeed after the provider updates snapshot and candidates");
  assert_true(alerts_result.rows_json.find("\"dataset\":\"alerts\"") != std::string::npos,
              "hot-updated request should return the payload from the new dataset route");
}

}  // namespace

int main() {
  try {
    test_data_query_lane_populates_cache_on_miss_and_reuses_hit();
    test_data_query_lane_returns_data_stale_for_strict_stale_cache();
    test_data_query_lane_allows_stale_cache_reads_when_requested();
    test_data_query_lane_rejects_query_receipts_with_side_effects();
    test_data_query_lane_lists_capabilities_without_using_cache();
    test_data_query_lane_resolves_hot_updated_route_views_without_rebuilding_lane();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}