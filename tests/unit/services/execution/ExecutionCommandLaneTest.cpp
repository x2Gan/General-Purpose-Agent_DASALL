#include <exception>
#include <iostream>
#include <optional>

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
using dasall::services::internal::AdapterRouteRequestKind;
using dasall::services::internal::AdapterRouter;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::ExecutionCommandLane;
using dasall::services::internal::ExecutionCommandLaneDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServicePolicyView;

class FakeInvoker final : public IAdapterInvoker {
 public:
  explicit FakeInvoker(
      std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke)
      : invoke_(std::move(invoke)) {}

  [[nodiscard]] std::string_view adapter_id() const override { return "service-primary"; }
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
      .request_id = "req-015",
      .session_id = "session-015",
      .trace_id = "trace-015",
      .tool_call_id = "tool-015",
      .goal_id = "goal-015",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] ExecutionCommandRequest make_request(std::string action = "toggle",
                                                   std::optional<std::string> idempotency_key =
                                                       std::string("idem-015")) {
  return ExecutionCommandRequest{
      .context = make_context(),
      .target = CapabilityTargetRef{
          .capability_id = "cap.exec",
          .target_id = "target-015",
      },
      .action = std::move(action),
      .arguments_json = "{\"desired_state\":\"on\"}",
      .idempotency_key = std::move(idempotency_key),
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"toggle", "safe_mode.enter"},
      .supported_queries = {},
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

void test_execution_command_lane_executes_successful_command_and_caches_idempotent_result() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int invoke_count = 0;
  const FakeInvoker invoker([&](const AdapterInvocationRequest& request) {
    ++invoke_count;
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = request.payload_json,
        .latency_ms = 5U,
        .side_effects = {"switch.enabled"},
        .evidence_refs = {"audit://execution/success-015"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
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

  const auto request = make_request();
  const auto first = lane.execute(request.context, request);
  const auto second = lane.execute(request.context, request);

  assert_true(!first.error.has_value(), "successful command should not surface an error");
  assert_equal(1,
               invoke_count,
               "same idempotency key should reuse cached result without reinvoking the adapter");
  assert_equal(std::string("exec:idem-015"),
               first.execution_id,
               "execution_id should derive from idempotency key for replay stability");
  assert_equal(first.execution_id,
               second.execution_id,
               "cached result should preserve the original execution_id");
  assert_equal(1,
               static_cast<int>(first.side_effects.size()),
               "successful command should preserve side_effect facts");
}

void test_execution_command_lane_rejects_invalid_request_before_routing() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int invoke_count = 0;
  const FakeInvoker invoker([&](const AdapterInvocationRequest&) {
    ++invoke_count;
    return AdapterInvocationResult{};
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
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

  auto invalid_request = make_request("");
  const auto result = lane.execute(invalid_request.context, invalid_request);

  assert_equal(0,
               invoke_count,
               "invalid request should fail before adapter invocation");
  assert_true(result.error.has_value(), "invalid request should return structured error info");
  assert_true(result.error->failure_type.has_value(),
              "invalid request should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "invalid request should map to validation failure type");
  assert_equal(std::string("capability_id, target_id, and action are required"),
               result.error->details.message,
               "invalid request should preserve the validation message");
}

void test_execution_command_lane_surfaces_partial_side_effect_with_compensation_hints() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const FakeInvoker invoker([](const AdapterInvocationRequest&) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::partial,
        .provider_status_code = "partial_side_effect",
        .payload_json = "{\"status\":\"partial\"}",
        .latency_ms = 8U,
        .side_effects = {"switch.enabled"},
        .evidence_refs = {"audit://execution/partial-015"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .critical_actions = {"toggle"},
      .high_risk_actions = {},
      .allow_high_risk_actions = false,
      .lookup_compensation_hints = [](const std::string&, const std::string&, const std::string&,
                                      const dasall::services::internal::AdapterReceipt&) {
        return std::vector<std::string>{"switch.disable", "idempotency:required"};
      },
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
  });

  const auto result = lane.execute(make_context(), make_request());

  assert_true(result.error.has_value(),
              "partial side effect should surface a structured provider error");
  assert_true(result.error->failure_type.has_value(),
              "partial side effect should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(*result.error->failure_type),
               "partial side effect should map to provider failure type");
  assert_equal(1,
               static_cast<int>(result.side_effects.size()),
               "partial side effect should preserve side_effects");
  assert_equal(2,
               static_cast<int>(result.compensation_hints.size()),
               "partial side effect should expose compensation hints for later recovery");
  assert_equal(std::string("audit://execution/partial-015"),
               result.error->source_ref.ref_id,
               "partial side effect should anchor source_ref on evidence_refs");
}

void test_execution_command_lane_serializes_critical_actions() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int invoke_count = 0;
  const FakeInvoker invoker([&](const AdapterInvocationRequest&) {
    ++invoke_count;
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = "{\"status\":\"ok\"}",
        .latency_ms = 6U,
        .side_effects = {"switch.enabled"},
        .evidence_refs = {"audit://execution/serial-015"},
    };
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  std::optional<dasall::services::ExecutionCommandResult> reentrant_result;
  bool reentered = false;
  ExecutionCommandLane* lane_ptr = nullptr;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
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
      .on_serialization_acquired = [&](const std::string&) {
        if (reentered || lane_ptr == nullptr) {
          return;
        }

        reentered = true;
        reentrant_result = lane_ptr->execute(make_context(), make_request("toggle", std::string("idem-015-b")));
      },
  });
  lane_ptr = &lane;

  const auto outer_result = lane.execute(make_context(), make_request());

  assert_true(!outer_result.error.has_value(),
              "outer execution should complete successfully once the lock is held");
  assert_true(reentrant_result.has_value(),
              "test hook should trigger a second critical action while the first is in flight");
  assert_true(reentrant_result->error.has_value(),
              "reentrant critical action should fail with structured busy error");
  assert_true(reentrant_result->error->failure_type.has_value(),
              "busy rejection should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(*reentrant_result->error->failure_type),
               "serialization conflict should map to provider failure type via target_busy");
  assert_equal(std::string("critical action is already in progress for target"),
               reentrant_result->error->details.message,
               "serialization conflict should surface a stable busy message");
  assert_equal(1,
               invoke_count,
               "busy reentry should be rejected before a second adapter invocation occurs");
}

void test_execution_command_lane_fail_closes_high_risk_actions_before_gate_08() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int invoke_count = 0;
  const FakeInvoker invoker([&](const AdapterInvocationRequest&) {
    ++invoke_count;
    return AdapterInvocationResult{};
  });
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .critical_actions = {"toggle"},
      .high_risk_actions = {"safe_mode.enter"},
      .allow_high_risk_actions = false,
      .lookup_compensation_hints = {},
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
  });

  const auto result = lane.execute(make_context(), make_request("safe_mode.enter"));

  assert_equal(0,
               invoke_count,
               "high-risk action should fail closed before adapter invocation when CAP-GATE-08 is not met");
  assert_true(result.error.has_value(),
              "gated high-risk action should return structured policy error");
  assert_true(result.error->failure_type.has_value(),
              "gated high-risk action should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Policy),
               static_cast<int>(*result.error->failure_type),
               "high-risk gate should map to policy failure type");
  assert_equal(std::string("high-risk action remains gated until CAP-GATE-08 is satisfied"),
               result.error->details.message,
               "high-risk gate should preserve the CAP-GATE-08 reason");
}

}  // namespace

int main() {
  try {
    test_execution_command_lane_executes_successful_command_and_caches_idempotent_result();
    test_execution_command_lane_rejects_invalid_request_before_routing();
    test_execution_command_lane_surfaces_partial_side_effect_with_compensation_hints();
    test_execution_command_lane_serializes_critical_actions();
    test_execution_command_lane_fail_closes_high_risk_actions_before_gate_08();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}