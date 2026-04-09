#include <exception>
#include <iostream>
#include <optional>

#include "execution/CompensationCatalog.h"
#include "execution/ExecutionCommandLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::CapabilityTargetRef;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ServiceCallContext;
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
using dasall::services::internal::CompensationCatalog;
using dasall::services::internal::CompensationCatalogEntry;
using dasall::services::internal::CompensationDescriptor;
using dasall::services::internal::ExecutionCommandLane;
using dasall::services::internal::ExecutionCommandLaneDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServicePolicyView;

class PartialInvoker final : public IAdapterInvoker {
 public:
  [[nodiscard]] std::string_view adapter_id() const override { return "service-primary"; }
  [[nodiscard]] AdapterRouteKind route_kind() const override {
    return AdapterRouteKind::local_service;
  }
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest&) const override {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::partial,
        .provider_status_code = "partial_side_effect",
        .payload_json = "{\"status\":\"partial\"}",
        .latency_ms = 7U,
        .side_effects = {"switch.enabled"},
        .evidence_refs = {"audit://execution/catalog-016"},
    };
  }
};

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-016",
      .session_id = "session-016",
      .trace_id = "trace-016",
      .tool_call_id = "tool-016",
      .goal_id = "goal-016",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] ExecutionCommandRequest make_request() {
  return ExecutionCommandRequest{
      .context = make_context(),
      .target = CapabilityTargetRef{
          .capability_id = "cap.exec",
          .target_id = "target-016",
      },
      .action = "toggle",
      .arguments_json = "{\"desired_state\":\"on\"}",
      .idempotency_key = std::string("idem-016"),
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"toggle"},
      .supported_queries = {},
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
      .requested_action_class = "command.standard",
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "command.standard",
      .allow_degrade = true,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_candidate() {
  return AdapterCandidateView{
      .adapter_id = "service-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "command.standard",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = {"cap.exec"},
  };
}

void test_compensation_catalog_returns_static_descriptor_for_known_entry() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const CompensationCatalog catalog;
  const auto descriptor = catalog.lookup("cap.exec", "toggle", "v1");

  assert_true(!descriptor.empty(), "known capability/action/version should resolve static hints");
  assert_equal(1,
               static_cast<int>(descriptor.compensation_hints.size()),
               "catalog should return at least one compensation hint for toggle");
  assert_equal(1,
               static_cast<int>(descriptor.idempotency_requirements.size()),
               "catalog should return idempotency requirements");
  assert_equal(1,
               static_cast<int>(descriptor.ordering_constraints.size()),
               "catalog should return action ordering constraints");
}

void test_compensation_catalog_returns_empty_descriptor_for_unknown_entry() {
  using dasall::tests::support::assert_true;

  const CompensationCatalog catalog;
  const auto descriptor = catalog.lookup("cap.exec", "unknown-action", "v1");

  assert_true(descriptor.empty(),
              "unknown capability/action/version should not synthesize compensation work");
}

void test_compensation_catalog_flattens_static_hints_for_command_lane_without_executing_compensation() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const CompensationCatalog catalog({
      CompensationCatalogEntry{
          .capability_id = "cap.exec",
          .action = "toggle",
          .capability_version = "v1",
          .descriptor = CompensationDescriptor{
              .compensation_hints = {"switch.disable"},
              .idempotency_requirements = {"reuse source_execution_id"},
              .ordering_constraints = {"disable after enable evidence"},
          },
      },
  });
  const PartialInvoker invoker;
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .compensation_catalog = &catalog,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .critical_actions = {"toggle"},
      .high_risk_actions = {},
      .allow_high_risk_actions = false,
      .lookup_compensation_hints = {},
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
  });

  const auto result = lane.execute(make_context(), make_request());

  assert_true(result.error.has_value(),
              "partial side effect should still surface a provider failure");
  assert_true(result.error->failure_type.has_value(),
              "partial side effect should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(*result.error->failure_type),
               "catalog-backed hints should preserve provider-side partial semantics");
  assert_equal(3,
               static_cast<int>(result.compensation_hints.size()),
               "catalog should emit hint, idempotency, and order facts without executing compensation");
  assert_true(result.compensation_hints[1].find("idempotency:") == 0,
              "flattened catalog output should prefix idempotency requirements");
  assert_true(result.compensation_hints[2].find("order:") == 0,
              "flattened catalog output should prefix ordering constraints");
}

}  // namespace

int main() {
  try {
    test_compensation_catalog_returns_static_descriptor_for_known_entry();
    test_compensation_catalog_returns_empty_descriptor_for_unknown_entry();
    test_compensation_catalog_flattens_static_hints_for_command_lane_without_executing_compensation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}