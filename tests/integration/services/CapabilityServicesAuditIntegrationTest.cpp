#include <exception>
#include <iostream>
#include <vector>

#include "ServiceFacade.h"
#include "audit/IAuditLogger.h"
#include "bridges/ServiceAuditBridge.h"
#include "execution/ExecutionCommandLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::CapabilityTargetRef;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ExecutionCompensationRequest;
using dasall::services::ServiceCallContext;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterBridge;
using dasall::services::internal::AdapterBridgeDependencies;
using dasall::services::internal::AdapterCandidateView;
using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::CapabilitySnapshotView;
using dasall::services::internal::ExecutionCommandLane;
using dasall::services::internal::ExecutionCommandLaneDependencies;
using dasall::services::internal::FallbackEnvelope;
using dasall::services::internal::IAdapterInvoker;
using dasall::services::internal::ResultMapper;
using dasall::services::internal::ServiceAuditBridge;
using dasall::services::internal::ServiceContextBuilder;
using dasall::services::internal::ServiceFacade;
using dasall::services::internal::ServiceFacadeDependencies;
using dasall::services::internal::ServicePolicyView;

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
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
    const auto side_effect = request.operation_name == "safe_mode.enter"
                                 ? std::string("safe_mode.enabled")
                                 : std::string("safe_mode.disabled");
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = request.payload_json,
        .latency_ms = 7U,
        .side_effects = {side_effect},
        .evidence_refs = {std::string("audit://services/") +
                          request.operation_name},
    };
  }
};

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000;

  return ServiceCallContext{
      .request_id = "req-024-int",
      .session_id = "session-024-int",
      .trace_id = "trace-024-int",
      .tool_call_id = "tool-call-024-int",
      .goal_id = "goal-024-int",
      .budget_guard = budget,
      .deadline_ms = 12000,
  };
}

[[nodiscard]] CapabilityTargetRef make_target() {
  return CapabilityTargetRef{
      .capability_id = "cap.exec",
      .target_id = "target-024-int",
  };
}

[[nodiscard]] CapabilitySnapshotView make_snapshot() {
  return CapabilitySnapshotView{
      .capability_id = "cap.exec",
      .capability_version = "v1",
      .supported_actions = {"safe_mode.enter", "safe_mode.exit"},
      .supported_queries = {},
      .route_classes = {AdapterRouteKind::local_service},
      .preferred_locality = AdapterRouteKind::local_service,
  };
}

[[nodiscard]] ServicePolicyView make_policy_view() {
  ServicePolicyView policy_view{};
  policy_view.high_risk_confirmation_required = true;
  policy_view.audit_level = "full";
  policy_view.observability_bridge_enabled = true;
  policy_view.adapter_preference_order = {AdapterRouteKind::local_service};
  return policy_view;
}

[[nodiscard]] FallbackEnvelope make_fallback_envelope() {
  return FallbackEnvelope{
      .requested_action_class = "command.high_risk",
      .ordered_candidates = {AdapterRouteKind::local_service},
      .route_equivalence_class = "command.high_risk",
      .allow_degrade = false,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] AdapterCandidateView make_candidate() {
  return AdapterCandidateView{
      .adapter_id = "service-primary",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "command.high_risk",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
      .supported_capabilities = {"cap.exec"},
  };
}

[[nodiscard]] bool has_action(const std::vector<dasall::infra::AuditEvent>& events,
                              const std::string& action) {
  return std::find_if(events.begin(), events.end(), [&](const auto& event) {
           return event.action == action;
         }) != events.end();
}

void test_capability_services_audit_integration_flows_through_facade_and_lane() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScriptedAuditLogger logger;
  ServiceAuditBridge audit_bridge(&logger);
  const ScriptedInvoker invoker;
  const AdapterBridge bridge(AdapterBridgeDependencies{.invokers = {&invoker}});
  const dasall::services::internal::AdapterRouter router;
  const ResultMapper mapper;

  ExecutionCommandLane lane(ExecutionCommandLaneDependencies{
      .router = &router,
      .bridge = &bridge,
      .result_mapper = &mapper,
      .compensation_catalog = nullptr,
      .policy_view = make_policy_view(),
      .capability_snapshot = make_snapshot(),
      .fallback_envelope = make_fallback_envelope(),
      .registered_candidates = {make_candidate()},
      .critical_actions = {"safe_mode.enter", "safe_mode.exit"},
      .high_risk_actions = {"safe_mode.enter"},
      .allow_high_risk_actions = true,
      .lookup_compensation_hints = {},
      .make_execution_id = {},
      .make_compensation_execution_id = {},
      .on_serialization_acquired = {},
      .audit_bridge = &audit_bridge,
  });

  ServiceContextBuilder context_builder;
  ServiceFacade facade(ServiceFacadeDependencies{
      .context_builder = &context_builder,
      .execute_command = [&](const ServiceCallContext& context,
                             const ExecutionCommandRequest& request) {
        return lane.execute(context, request);
      },
      .compensate_command = [&](const ServiceCallContext& context,
                                const ExecutionCompensationRequest& request) {
        return lane.compensate(context, request);
      },
      .query_execution_state = {},
      .subscribe_execution_state = {},
      .diagnose_execution_target = {},
      .query_data = {},
      .list_data_capabilities = {},
  });

  const auto execute_result = facade.execute(ExecutionCommandRequest{
      .context = make_context(),
      .target = make_target(),
      .action = "safe_mode.enter",
      .arguments_json = "{}",
      .idempotency_key = std::string("idem-024-int"),
  });
  const auto compensate_result = facade.compensate(ExecutionCompensationRequest{
      .context = make_context(),
      .target = make_target(),
      .compensation_action = "safe_mode.exit",
      .arguments_json = "{}",
      .source_execution_id = execute_result.execution_id,
      .reason_code = "manual_recovery",
  });

  assert_true(!execute_result.error.has_value() && !compensate_result.error.has_value(),
              "services audit integration should keep successful command and compensation execution results intact");
  assert_equal(4,
               static_cast<int>(logger.events.size()),
               "facade -> command lane -> audit bridge integration should emit four audit events for request/completion and compensation request/completion");
  assert_true(has_action(logger.events, "service.execution.requested") &&
                  has_action(logger.events, "service.execution.completed") &&
                  has_action(logger.events, "service.execution.compensation_requested") &&
                  has_action(logger.events, "service.execution.compensation_completed"),
              "services audit integration should preserve the frozen service audit event family names across facade dispatch");
  assert_equal(std::string("services.execution"),
               logger.contexts.front().worker_type,
               "services audit integration should keep audit events on the dedicated services.execution worker type");
  assert_equal(4,
               static_cast<int>(audit_bridge.get_status().emitted_total),
               "services audit bridge status should track the number of persisted audit events during integration flow");
}

}  // namespace

int main() {
  try {
    test_capability_services_audit_integration_flows_through_facade_and_lane();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}