#include <exception>
#include <iostream>
#include <vector>

#include "audit/IAuditLogger.h"
#include "bridges/ServiceAuditBridge.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.erase(scripted_outcomes.begin());
      return outcome;
    }

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

  std::vector<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] dasall::services::ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000;

  return dasall::services::ServiceCallContext{
      .request_id = "req-024",
      .session_id = "session-024",
      .trace_id = "trace-024",
      .tool_call_id = "tool-call-024",
      .goal_id = "goal-024",
      .budget_guard = budget,
      .deadline_ms = 12000,
  };
}

[[nodiscard]] dasall::services::CapabilityTargetRef make_target() {
  return dasall::services::CapabilityTargetRef{
      .capability_id = "cap.exec",
      .target_id = "target-024",
  };
}

[[nodiscard]] dasall::services::ExecutionCommandResult make_success_result(
    std::string execution_id,
    std::vector<std::string> side_effects) {
  return dasall::services::ExecutionCommandResult{
      .code = std::nullopt,
      .execution_id = std::move(execution_id),
      .payload_json = "{\"status\":\"ok\"}",
      .side_effects = std::move(side_effects),
      .compensation_hints = {},
      .error = std::nullopt,
  };
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

void test_service_audit_bridge_emits_execution_and_fallback_events() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::services::internal::ServiceAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScriptedAuditLogger logger;
  ServiceAuditBridge bridge(&logger);

  const auto requested = bridge.write_execution_requested(
      make_context(), make_target(), "safe_mode.enter", "exec-024", true);
  const auto completed = bridge.write_execution_completed(
      make_context(),
      make_target(),
      "safe_mode.enter",
      make_success_result("exec-024", {"safe_mode.enabled"}));
  const auto fallback_blocked = bridge.write_fallback_blocked(
      make_context(),
      make_target(),
      "safe_mode.enter",
      "exec-024",
      "fallback_blocked",
      "command.high_risk");

  assert_true(requested.emitted && completed.emitted && fallback_blocked.emitted,
              "service audit bridge should emit required execution and fallback audit events when a logger sink is present");
  assert_equal(3,
               static_cast<int>(logger.events.size()),
               "service audit bridge should write one audit record per requested event");

  const auto& requested_event = logger.events[0];
  assert_equal(std::string("service.execution.requested"),
               requested_event.action,
               "execution request audit should use the frozen service.execution.requested name");
  assert_true(requested_event.outcome == AuditOutcome::Escalated,
              "execution request audit should mark the request as escalated into the controlled execution path");
  assert_true(requested_event.evidence_ref.kind == AuditEvidenceKind::ToolResult,
              "execution request audit should keep execution correlation on ToolResult evidence refs");
  assert_true(has_side_effect(requested_event, "action:safe_mode.enter") &&
                  has_side_effect(requested_event, "require_confirmation:true"),
              "execution request audit should preserve action and confirmation facts as side effects");

  const auto& completed_event = logger.events[1];
  assert_equal(std::string("service.execution.completed"),
               completed_event.action,
               "execution completion audit should use the frozen service.execution.completed name");
  assert_true(completed_event.outcome == AuditOutcome::Succeeded,
              "successful execution completion audit should map to AuditOutcome::Succeeded");
  assert_true(has_side_effect(completed_event, "safe_mode.enabled") &&
                  has_side_effect(completed_event, "execution_id:exec-024"),
              "execution completion audit should carry both provider side effects and the execution id");

  const auto& fallback_event = logger.events[2];
  assert_equal(std::string("service.route.fallback_blocked"),
               fallback_event.action,
               "fallback-blocked audit should use the frozen service.route.fallback_blocked name");
  assert_true(fallback_event.outcome == AuditOutcome::Rejected,
              "fallback-blocked audit should map to AuditOutcome::Rejected");
  assert_true(has_side_effect(fallback_event, "deny_reason:fallback_blocked") &&
                  has_side_effect(fallback_event, "action_class:command.high_risk"),
              "fallback-blocked audit should preserve deny reason and action class facts");
  assert_equal(std::string("tool_call://tool-call-024"),
               requested_event.actor,
               "service audit bridge should normalize actor from tool_call_id");
  assert_equal(std::string("cap.exec:target-024"),
               requested_event.target,
               "service audit bridge should keep capability and target correlation in the audit target field");
  assert_equal(std::string("services.execution"),
               logger.contexts.front().worker_type,
               "service audit bridge should pin worker_type=services.execution on emitted audit contexts");
}

void test_service_audit_bridge_emits_compensation_events_with_recovery_refs() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::services::ExecutionCompensationRequest;
  using dasall::services::internal::ServiceAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScriptedAuditLogger logger;
  ServiceAuditBridge bridge(&logger);

  const auto request = ExecutionCompensationRequest{
      .context = make_context(),
      .target = make_target(),
      .compensation_action = "safe_mode.exit",
      .arguments_json = "{}",
      .source_execution_id = "exec-024",
      .reason_code = "manual_recovery",
  };

  const auto requested = bridge.write_compensation_requested(
      request.context,
      request,
      "comp:exec-024:safe_mode.exit");
  const auto completed = bridge.write_compensation_completed(
      request.context,
      request,
      make_success_result("comp:exec-024:safe_mode.exit", {"safe_mode.disabled"}));

  assert_true(requested.emitted && completed.emitted,
              "service audit bridge should emit both compensation request and completion audit events");
  assert_equal(2,
               static_cast<int>(logger.events.size()),
               "compensation audit should append exactly two audit events for request and completion");
  assert_true(logger.events.front().evidence_ref.kind == AuditEvidenceKind::RecoveryOutcome &&
                  logger.events.back().evidence_ref.kind == AuditEvidenceKind::RecoveryOutcome,
              "compensation audit should keep recovery-path correlation on RecoveryOutcome evidence refs");
  assert_true(logger.events.front().outcome == AuditOutcome::Escalated &&
                  logger.events.back().outcome == AuditOutcome::Succeeded,
              "compensation audit should mark request as escalated and successful completion as succeeded");
  assert_true(has_side_effect(logger.events.front(), "source_execution_id:exec-024") &&
                  has_side_effect(logger.events.back(), "reason_code:manual_recovery") &&
                  has_side_effect(logger.events.back(), "safe_mode.disabled"),
              "compensation audit should preserve source execution, reason code and resulting side effects");
  assert_equal(std::string("service.execution.compensation_requested"),
               logger.events.front().action,
               "compensation request audit should use the frozen compensation_requested action name");
  assert_equal(std::string("service.execution.compensation_completed"),
               logger.events.back().action,
               "compensation completion audit should use the frozen compensation_completed action name");
}

void test_service_audit_bridge_surfaces_missing_sink_as_local_failure() {
  using dasall::contracts::ResultCode;
  using dasall::services::internal::ServiceAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ServiceAuditBridge bridge;
  const auto result = bridge.write_execution_requested(
      make_context(), make_target(), "safe_mode.enter", "exec-024", true);
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid(),
              "service audit bridge should surface a missing audit sink as a structured local failure");
  assert_true(result.result_code == ResultCode::RuntimeRetryExhausted &&
                  result.references_only_contract_error_types(),
              "service audit bridge should normalize missing sinks to RuntimeRetryExhausted without inventing new failure families");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1,
              "service audit bridge should retain missing-sink degradation state for later health decisions");
  assert_equal(static_cast<int>(ResultCode::RuntimeRetryExhausted),
               static_cast<int>(*status.last_error_code),
               "service audit bridge should record RuntimeRetryExhausted as the last missing-sink failure");
}

}  // namespace

int main() {
  try {
    test_service_audit_bridge_emits_execution_and_fallback_events();
    test_service_audit_bridge_emits_compensation_events_with_recovery_refs();
    test_service_audit_bridge_surfaces_missing_sink_as_local_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}