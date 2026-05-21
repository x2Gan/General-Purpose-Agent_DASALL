#include <exception>
#include <iostream>

#include "adapters/AdapterRouter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterRouteFailure;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterRouteRequest;
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterRouter;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::ServicePolicyView;

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"safe_mode.enter", "toggle"},
      .supported_queries = {"state"},
      .route_classes = {
          AdapterRouteKind::local_platform,
          AdapterRouteKind::local_service,
          AdapterRouteKind::remote_service,
      },
      .preferred_locality = AdapterRouteKind::local_platform,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view(bool local_platform_enabled = true) {
  ServicePolicyView policy_view{};
  policy_view.local_platform_route_enabled = local_platform_enabled;
  policy_view.adapter_preference_order = {
      AdapterRouteKind::local_platform,
      AdapterRouteKind::local_service,
      AdapterRouteKind::remote_service,
  };
  return policy_view;
}

[[nodiscard]] FallbackEnvelope make_envelope(bool allow_degrade = true,
                                             std::string deny_reason = "fallback_blocked") {
  return FallbackEnvelope{
      .requested_action_class = "high_risk_control",
      .ordered_candidates = {
          AdapterRouteKind::local_platform,
          AdapterRouteKind::local_service,
          AdapterRouteKind::remote_service,
      },
      .route_equivalence_class = "safe-mode-control",
      .allow_degrade = allow_degrade,
      .deny_reason_on_exhaustion = std::move(deny_reason),
  };
}

[[nodiscard]] AdapterCandidateView make_candidate(std::string adapter_id,
                                                  AdapterRouteKind route_kind,
                                                  std::string equivalence_class,
                                                  AdapterTrustClass trust_class,
                                                  AdapterAvailabilityState availability_state) {
  return AdapterCandidateView{
      .adapter_id = std::move(adapter_id),
      .route_kind = route_kind,
      .route_equivalence_class = std::move(equivalence_class),
      .trust_class = trust_class,
      .availability_state = availability_state,
      .supported_capabilities = {"cap.exec"},
  };
}

[[nodiscard]] AdapterRouteRequest make_request() {
  return AdapterRouteRequest{
      .capability_id = "cap.exec",
      .target_id = "target-035",
      .request_kind = AdapterRouteRequestKind::action,
      .requested_operation = "safe_mode.enter",
      .high_risk = true,
      .minimum_trust = AdapterTrustClass::caller_verified,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_envelope(),
      .registered_candidates = {},
  };
}

void test_adapter_router_selects_preferred_platform_route() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.registered_candidates = {
      make_candidate("platform-primary",
                     AdapterRouteKind::local_platform,
                     "safe-mode-control",
                     AdapterTrustClass::trusted_local,
                     AdapterAvailabilityState::available),
      make_candidate("service-fallback",
                     AdapterRouteKind::local_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(decision.ok(), "preferred platform route should be selected when available");
  assert_equal(static_cast<int>(AdapterRouteKind::local_platform),
               static_cast<int>(decision.selection->route_kind),
               "router should prefer local_platform when it is enabled and healthy");
  assert_equal(std::string("platform-primary"),
               decision.selection->adapter_id,
               "router should return the preferred adapter_id");
  assert_equal(0,
               static_cast<int>(decision.selection->fallback_hop),
               "preferred route should not consume a fallback hop");
}

void test_adapter_router_skips_platform_route_when_profile_disables_it() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.policy_view = make_policy_view(false);
  request.registered_candidates = {
      make_candidate("platform-disabled",
                     AdapterRouteKind::local_platform,
                     "safe-mode-control",
                     AdapterTrustClass::trusted_local,
                     AdapterAvailabilityState::available),
      make_candidate("service-active",
                     AdapterRouteKind::local_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(decision.ok(), "router should choose the next allowed route when platform is off");
  assert_equal(static_cast<int>(AdapterRouteKind::local_service),
               static_cast<int>(decision.selection->route_kind),
               "router should not pick local_platform when profile disables it");
  assert_equal(std::string("service-active"),
               decision.selection->adapter_id,
               "router should route to the remaining eligible service adapter");
}

void test_adapter_router_uses_equivalent_fallback_when_primary_is_unavailable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.registered_candidates = {
      make_candidate("platform-down",
                     AdapterRouteKind::local_platform,
                     "safe-mode-control",
                     AdapterTrustClass::trusted_local,
                     AdapterAvailabilityState::unavailable),
      make_candidate("service-fallback",
                     AdapterRouteKind::local_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(decision.ok(), "router should fallback within the runtime envelope");
  assert_equal(static_cast<int>(AdapterRouteKind::local_service),
               static_cast<int>(decision.selection->route_kind),
               "router should select the semantically equivalent fallback route");
  assert_equal(1,
               static_cast<int>(decision.selection->fallback_hop),
               "router should report the consumed fallback hop");
}

void test_adapter_router_blocks_semantically_different_fallbacks() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.registered_candidates = {
      make_candidate("platform-down",
                     AdapterRouteKind::local_platform,
                     "safe-mode-control",
                     AdapterTrustClass::trusted_local,
                     AdapterAvailabilityState::unavailable),
      make_candidate("remote-mismatch",
                     AdapterRouteKind::remote_service,
                     "remote-proxy",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(!decision.ok(), "router should fail closed when only non-equivalent fallback exists");
  assert_equal(static_cast<int>(AdapterRouteFailure::fallback_blocked),
               static_cast<int>(decision.failure),
               "router should surface fallback_blocked for out-of-envelope routes");
}

void test_adapter_router_rejects_candidate_with_insufficient_trust() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.minimum_trust = AdapterTrustClass::trusted_local;
  request.registered_candidates = {
      make_candidate("remote-limited",
                     AdapterRouteKind::remote_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(!decision.ok(), "router should reject candidates below the required trust level");
  assert_equal(static_cast<int>(AdapterRouteFailure::route_not_permitted),
               static_cast<int>(decision.failure),
               "router should report route_not_permitted for trust mismatches");
}

void test_adapter_router_rejects_snapshot_mismatch_before_candidate_scan() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.capability_id = "cap.exec.beta";
  request.registered_candidates = {
      make_candidate("service-primary",
                     AdapterRouteKind::local_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::available),
  };

  const auto decision = router.select_adapter(request);

  assert_true(!decision.ok(), "router should fail closed when snapshot capability_id mismatches");
  assert_equal(static_cast<int>(AdapterRouteFailure::capability_unsupported),
               static_cast<int>(decision.failure),
               "snapshot mismatch should surface capability_unsupported");
}

void test_adapter_router_fail_closes_unknown_availability_candidates() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRouter router;
  auto request = make_request();
  request.registered_candidates = {
      make_candidate("service-unknown",
                     AdapterRouteKind::local_service,
                     "safe-mode-control",
                     AdapterTrustClass::caller_verified,
                     AdapterAvailabilityState::unknown),
  };

  const auto decision = router.select_adapter(request);

  assert_true(!decision.ok(), "router should fail closed when candidate availability is unknown");
  assert_equal(static_cast<int>(AdapterRouteFailure::route_unavailable),
               static_cast<int>(decision.failure),
               "unknown availability should be treated as no selectable route");
}

}  // namespace

int main() {
  try {
    test_adapter_router_selects_preferred_platform_route();
    test_adapter_router_skips_platform_route_when_profile_disables_it();
    test_adapter_router_uses_equivalent_fallback_when_primary_is_unavailable();
    test_adapter_router_blocks_semantically_different_fallbacks();
    test_adapter_router_rejects_candidate_with_insufficient_trust();
    test_adapter_router_rejects_snapshot_mismatch_before_candidate_scan();
    test_adapter_router_fail_closes_unknown_availability_candidates();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}