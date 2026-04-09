#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "ServiceTypes.h"
#include "data/DataProjectionCache.h"
#include "ops/ServiceConfigAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::profiles::BuildProfileManifest;
using dasall::profiles::BuildProfileResolveRequest;
using dasall::profiles::BuildProfileResolver;
using dasall::profiles::ProfileCatalog;
using dasall::profiles::RuntimePolicyLoadRequest;
using dasall::profiles::RuntimePolicyProvider;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::services::DataQueryRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::DataProjectionCache;
using dasall::services::internal::ProjectionCacheState;
using dasall::services::internal::ServiceConfigAdapter;
using dasall::services::internal::ServicePolicyView;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct LoadedServicePolicy {
  BuildProfileManifest build_manifest;
  std::shared_ptr<const RuntimePolicySnapshot> runtime_snapshot;
  ServicePolicyView policy_view;
};

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] LoadedServicePolicy load_service_policy(const std::string& profile_id,
                                                     const std::string& expected_target_platform) {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);
  const ServiceConfigAdapter adapter;

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = profile_id,
      .expected_target_platform = expected_target_platform,
  });
  assert_true(manifest_result.ok(),
              "profile integration should resolve a valid build manifest from the real profile assets");

  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = profile_id,
  });
  assert_true(runtime_result.ok(),
              "profile integration should load a valid runtime snapshot from the real profile assets");

  const auto derivation = adapter.derive_policy_view(*runtime_result.snapshot,
                                                     *manifest_result.manifest);
  assert_true(derivation.ok(),
              "profile integration should derive a services policy view from the resolved assets");

  return LoadedServicePolicy{
      .build_manifest = *manifest_result.manifest,
      .runtime_snapshot = runtime_result.snapshot,
      .policy_view = *derivation.policy_view,
  };
}

[[nodiscard]] ServiceDataFreshness default_freshness(const ServicePolicyView& policy_view) {
  return policy_view.default_allow_stale_reads ? ServiceDataFreshness::allow_stale
                                               : ServiceDataFreshness::strict;
}

[[nodiscard]] DataQueryRequest make_data_query_request() {
  return DataQueryRequest{
      .context = ServiceCallContext{
          .request_id = "req-services-profile-cache",
          .session_id = "session-services-profile-cache",
          .trace_id = "trace-services-profile-cache",
          .tool_call_id = "tool-services-profile-cache",
          .goal_id = "goal-services-profile-cache",
          .budget_guard = std::nullopt,
          .deadline_ms = 1712746905000ULL,
      },
      .dataset = "inventory.devices",
      .filters_json = "{\"site\":\"factory-a\"}",
      .projection = "summary",
      .freshness = ServiceDataFreshness::strict,
  };
}

void test_capability_services_profile_integration_derives_route_and_timeout_differences() {
  const auto desktop = load_service_policy("desktop_full",
                                           "linux-x86_64-workstation");
  const auto edge = load_service_policy("edge_balanced",
                                        "linux-arm64-embedded");

  assert_equal(std::string("desktop_full"),
               desktop.policy_view.effective_profile_id,
               "desktop_full should remain the effective profile id after policy derivation");
  assert_equal(std::string("edge_balanced"),
               edge.policy_view.effective_profile_id,
               "edge_balanced should remain the effective profile id after policy derivation");
  assert_true(!desktop.policy_view.local_platform_route_enabled,
              "desktop_full should keep platform routing disabled because platform_hal is off in the real manifest");
  assert_true(edge.policy_view.local_platform_route_enabled,
              "edge_balanced should enable local platform routing because platform_hal is on in the real manifest");
  assert_true(desktop.policy_view.adapter_preference_order.size() == 2U &&
                  desktop.policy_view.adapter_preference_order.front() ==
                      AdapterRouteKind::local_service &&
                  desktop.policy_view.adapter_preference_order.back() ==
                      AdapterRouteKind::remote_service,
              "desktop_full should keep the local_service to remote_service route order when no platform route is available");
  assert_true(edge.policy_view.adapter_preference_order.size() == 3U &&
                  edge.policy_view.adapter_preference_order.front() ==
                      AdapterRouteKind::local_platform &&
                  edge.policy_view.adapter_preference_order[1] ==
                      AdapterRouteKind::local_service &&
                  edge.policy_view.adapter_preference_order[2] ==
                      AdapterRouteKind::remote_service,
              "edge_balanced should prepend local_platform ahead of the shared service and remote routes");
  assert_equal(8000,
               static_cast<int>(desktop.policy_view.request_deadline_ceiling_ms),
               "desktop_full should derive the request deadline ceiling from its runtime budget latency");
  assert_equal(7000,
               static_cast<int>(edge.policy_view.request_deadline_ceiling_ms),
               "edge_balanced should derive a lower request deadline ceiling from its runtime budget latency");
  assert_equal(2500,
               static_cast<int>(desktop.policy_view.adapter_call_timeout_ms),
               "desktop_full should derive the tool adapter timeout from its runtime policy");
  assert_equal(1800,
               static_cast<int>(edge.policy_view.adapter_call_timeout_ms),
               "edge_balanced should derive the shorter tool adapter timeout from its runtime policy");
  assert_equal(5000,
               static_cast<int>(desktop.policy_view.orchestration_timeout_ms),
               "desktop_full should preserve its workflow timeout in the derived services policy");
  assert_equal(4000,
               static_cast<int>(edge.policy_view.orchestration_timeout_ms),
               "edge_balanced should preserve its lower workflow timeout in the derived services policy");
}

void test_capability_services_profile_integration_applies_real_cache_ttls_and_default_freshness() {
  const auto desktop = load_service_policy("desktop_full",
                                           "linux-x86_64-workstation");
  const auto edge = load_service_policy("edge_balanced",
                                        "linux-arm64-embedded");

  std::uint64_t desktop_now_ms = 0U;
  std::uint64_t edge_now_ms = 0U;

  DataProjectionCache desktop_cache({
      .ttl_ms = static_cast<std::uint64_t>(desktop.policy_view.data_cache_ttl_ms),
      .now_ms = [&desktop_now_ms]() {
        return desktop_now_ms;
      },
  });
  DataProjectionCache edge_cache({
      .ttl_ms = static_cast<std::uint64_t>(edge.policy_view.data_cache_ttl_ms),
      .now_ms = [&edge_now_ms]() {
        return edge_now_ms;
      },
  });

  const auto request = make_data_query_request();
  desktop_cache.store(request,
                      "[{\"device\":\"desktop\"}]",
                      "cache://profiles/desktop_full/inventory.devices");
  edge_cache.store(request,
                   "[{\"device\":\"edge\"}]",
                   "cache://profiles/edge_balanced/inventory.devices");

  desktop_now_ms = 150000U;
  edge_now_ms = 150000U;

  const auto desktop_default_lookup = desktop_cache.lookup(
      request,
      default_freshness(desktop.policy_view));
  const auto edge_default_lookup = edge_cache.lookup(
      request,
      default_freshness(edge.policy_view));
  const auto edge_strict_lookup = edge_cache.lookup(request,
                                                    ServiceDataFreshness::strict);

  assert_true(desktop_default_lookup.state == ProjectionCacheState::hit_fresh &&
                  desktop_default_lookup.from_cache,
              "desktop_full should keep a 150s cache entry fresh under its 180s TTL");
  assert_true(edge_default_lookup.state == ProjectionCacheState::hit_stale &&
                  edge_default_lookup.from_cache,
              "edge_balanced should serve the same 150s cache entry from cache because its shorter TTL expires earlier and its default freshness allows stale reads");
  assert_true(edge_strict_lookup.state == ProjectionCacheState::hit_stale &&
                  !edge_strict_lookup.from_cache,
              "edge_balanced should still reject stale cache reads when callers explicitly require strict freshness");
  assert_true(edge_default_lookup.snapshot.has_value() &&
                  edge_default_lookup.snapshot->age_ms == 150000U,
              "edge_balanced stale cache evidence should preserve the observed age for diagnostics");

  desktop_now_ms = 182000U;
  edge_now_ms = 182000U;

  const auto desktop_expired_default_lookup = desktop_cache.lookup(
      request,
      default_freshness(desktop.policy_view));
  const auto desktop_expired_allow_stale_lookup = desktop_cache.lookup(
      request,
      ServiceDataFreshness::allow_stale);
  const auto edge_expired_default_lookup = edge_cache.lookup(
      request,
      default_freshness(edge.policy_view));

  assert_true(desktop_expired_default_lookup.state == ProjectionCacheState::hit_stale &&
                  !desktop_expired_default_lookup.from_cache,
              "desktop_full should stop serving stale cache entries once its longer TTL also expires under strict freshness");
  assert_true(desktop_expired_allow_stale_lookup.state == ProjectionCacheState::hit_stale &&
                  desktop_expired_allow_stale_lookup.from_cache,
              "desktop_full should still surface stale cache entries when callers explicitly opt into allow_stale");
  assert_true(edge_expired_default_lookup.state == ProjectionCacheState::hit_stale &&
                  edge_expired_default_lookup.from_cache,
              "edge_balanced should continue serving stale cache entries after expiry because its default freshness baseline remains allow_stale");
}

}  // namespace

int main() {
  try {
    test_capability_services_profile_integration_derives_route_and_timeout_differences();
    test_capability_services_profile_integration_applies_real_cache_ttls_and_default_freshness();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}