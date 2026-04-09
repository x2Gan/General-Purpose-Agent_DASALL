#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "ServiceTypes.h"
#include "adapters/AdapterRouter.h"
#include "execution/ExecutionCommandLane.h"
#include "execution/ExecutionSubscriptionHub.h"
#include "ops/ServiceHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::services::CapabilityTargetRef;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ExecutionSubscriptionRequest;
using dasall::services::ServiceCallContext;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::ExecutionCommandLane;
using dasall::services::internal::ExecutionCommandLaneDependencies;
using dasall::services::internal::ExecutionSubscriptionHub;
using dasall::services::internal::ExecutionSubscriptionHubDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IServiceHealthSignalProvider;
using dasall::services::internal::ServiceCircuitState;
using dasall::services::internal::ServiceHealthProbe;
using dasall::services::internal::ServiceHealthSample;
using dasall::services::internal::ServicePolicyView;

class StaticSignalProvider final : public IServiceHealthSignalProvider {
 public:
  explicit StaticSignalProvider(ServiceHealthSample sample)
      : sample_(std::move(sample)) {}

  [[nodiscard]] ServiceHealthSample sample(std::int64_t) override {
    return sample_;
  }

 private:
  ServiceHealthSample sample_;
};

[[nodiscard]] ServiceCallContext make_context(std::string request_id) {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000U;

  return ServiceCallContext{
      .request_id = std::move(request_id),
      .session_id = "session-027-int",
      .trace_id = "trace-027-int",
      .tool_call_id = "tool-027-int",
      .goal_id = "goal-027-int",
      .budget_guard = budget,
      .deadline_ms = 16000U,
  };
}

[[nodiscard]] CapabilityTargetRef make_target() {
  return CapabilityTargetRef{
      .capability_id = "cap.exec",
      .target_id = "target-027-int",
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  ServicePolicyView policy_view{};
  policy_view.effective_profile_id = "edge_balanced";
  policy_view.adapter_preference_order = {AdapterRouteKind::local_service};
  policy_view.observability_bridge_enabled = true;
  return policy_view;
}

[[nodiscard]] CapabilitySnapshotView make_capability_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"safe_mode.enter"},
      .supported_queries = {},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] FallbackEnvelope make_fallback_envelope() {
  return FallbackEnvelope{
      .requested_action_class = "command.standard",
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "service.local",
      .allow_degrade = true,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_unavailable_candidate() {
  return AdapterCandidateView{
      .adapter_id = "service-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "service.local",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::unavailable,
      .supported_capabilities = {"cap.exec"},
  };
}

[[nodiscard]] bool has_component(const dasall::infra::HealthSnapshot& snapshot,
                                 const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

void test_capability_services_health_integration_combines_circuit_adapter_and_queue_pressure() {
  using dasall::tests::support::assert_true;

  const dasall::services::internal::AdapterRouter router;
  ExecutionCommandLane command_lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = nullptr,
      .result_mapper = nullptr,
      .compensation_catalog = nullptr,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_capability_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_unavailable_candidate()},
      .critical_actions = {},
      .high_risk_actions = {},
      .allow_high_risk_actions = true,
      .lookup_compensation_hints = {},
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
      .audit_bridge = nullptr,
      .metrics_bridge = nullptr,
      .trace_bridge = nullptr,
  });

  const auto command_result = command_lane.execute(
      make_context("req-027-exec"),
      ExecutionCommandRequest{
          .context = make_context("req-027-exec"),
          .target = make_target(),
          .action = "query.status",
          .arguments_json = "{}",
          .idempotency_key = std::nullopt,
      });
  assert_true(command_result.error.has_value(),
              "route-unavailable command flow should surface a structured error before health aggregation runs");

  ExecutionSubscriptionHub hub(
      ExecutionSubscriptionHubDependencies{.max_buffered_events = 1U});
  hub.publish(make_target(), "status", {"{\"seq\":1}"});
  hub.publish(make_target(), "status", {"{\"seq\":2}"});

  const auto subscription_result = hub.subscribe(
      make_context("req-027-sub"),
      ExecutionSubscriptionRequest{
          .context = make_context("req-027-sub"),
          .target = make_target(),
          .stream_kind = "status",
          .cursor = std::string("0"),
          .max_events = 2U,
      });
  assert_true(subscription_result.resync_required && subscription_result.dropped_count == 1U,
              "subscription overflow should remain observable before it is folded into the services health probe");

  ServiceHealthSample sample;
  sample.circuit_state = ServiceCircuitState::open;
  sample.adapter_readiness = AdapterAvailabilityState::unavailable;
  sample.command_queue.high_watermark = 4U;
  sample.subscription_queue.depth = 1U;
  sample.subscription_queue.high_watermark = 1U;
  sample.subscription_queue.overflow_total = subscription_result.dropped_count;
  sample.subscription_queue.resync_required = subscription_result.resync_required;
  sample.sampled_at_unix_ms = 1712746800000;
  sample.latency_ms = 9;

  ServiceHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto probe_result = probe.probe();
  const auto snapshot = probe.snapshot();

  assert_true(probe_result.status == ProbeStatus::Degraded &&
                  snapshot.liveness && !snapshot.readiness && snapshot.degraded,
              "combined circuit-open, adapter-down, and queue-pressure facts should keep services live but not ready");
  assert_true(has_component(snapshot, "services.circuit") &&
                  has_component(snapshot, "services.adapter") &&
                  has_component(snapshot, "services.subscription_queue"),
              "integration health snapshot should retain all contributing failure components for infra health aggregation");
}

}  // namespace

int main() {
  try {
    test_capability_services_health_integration_combines_circuit_adapter_and_queue_pressure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}