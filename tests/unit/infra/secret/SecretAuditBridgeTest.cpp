#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "secret/SecretAuditBridge.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::secret::SecretAuditEvent make_secret_audit_event(
    std::string reason_code = "policy_allowed",
    std::string version = "v3",
    bool outcome = true) {
  using dasall::infra::secret::SecretAuditEvent;

  return SecretAuditEvent{
      .actor = std::string("runtime"),
      .action = dasall::infra::secret::SecretAuditAction::Unspecified,
      .target_secret = std::string("db/root"),
      .consumer_module = std::string("runtime"),
      .outcome = outcome,
      .reason_code = std::move(reason_code),
      .version = std::move(version),
      .evidence_ref = std::string("tool-call-001"),
      .request_id = std::string("req-001"),
      .task_id = std::string("task-001"),
  };
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);

    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
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
      const dasall::infra::ExportQuery& query) override {
    static_cast<void>(query);
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

void test_secret_audit_bridge_emits_complete_access_rotate_and_revoke_events() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::kAuditContextUnknown;
  using dasall::infra::secret::SecretAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  SecretAuditBridge bridge(logger);

  const auto access_result = bridge.emit_access_granted(make_secret_audit_event());
  const auto rotate_result = bridge.emit_rotate(
      make_secret_audit_event("scheduled_rotation", "v4", true));
  const auto revoke_result = bridge.emit_revoke(
      make_secret_audit_event("manual_revoke", "v4", true));

  assert_true(access_result.emitted && access_result.is_valid(),
              "SecretAuditBridge should emit a valid access_granted audit event with a complete AuditEvent and AuditContext payload");
  assert_true(rotate_result.emitted && rotate_result.is_valid(),
              "SecretAuditBridge should emit a valid rotated audit event through the shared audit logger sink");
  assert_true(revoke_result.emitted && revoke_result.is_valid(),
              "SecretAuditBridge should emit a valid revoked audit event through the shared audit logger sink");
  assert_equal(3, static_cast<int>(logger->events.size()),
               "SecretAuditBridge should dispatch one AuditEvent per access/rotate/revoke wrapper invocation");

  const auto& access_event = logger->events.front();
  const auto& access_context = logger->contexts.front();
  assert_equal(std::string("secret.access_granted"),
               access_event.action,
               "SecretAuditBridge should map AccessGranted to the frozen secret.access_granted action name");
  assert_equal(std::string("secret:db/root"),
               access_event.target,
               "SecretAuditBridge should prefix the target secret with the frozen secret: audit namespace");
  assert_true(access_event.outcome == AuditOutcome::Succeeded,
              "SecretAuditBridge should map a successful access event to AuditOutcome::Succeeded");
  assert_true(access_event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  access_event.evidence_ref.ref == "tool-call-001",
              "SecretAuditBridge should keep evidence_ref inside the frozen ToolResult audit evidence boundary");
  assert_true(has_side_effect(access_event, "consumer_module:runtime") &&
                  has_side_effect(access_event, "reason_code:policy_allowed") &&
                  has_side_effect(access_event, "version:v3"),
              "SecretAuditBridge should serialize consumer_module, reason_code and version into unique side_effects entries");
  assert_true(access_event.event_id.rfind("secret-audit-event-", 0) == 0 &&
                  access_event.timestamp > 0,
              "SecretAuditBridge should stamp every audit payload with an internal event_id and timestamp before dispatch");
  assert_equal(std::string("req-001"), access_context.request_id,
               "SecretAuditBridge should propagate request_id into AuditContext.request_id");
  assert_equal(std::string("task-001"), access_context.task_id,
               "SecretAuditBridge should propagate task_id into AuditContext.task_id");
  assert_equal(std::string("runtime"), access_context.worker_type,
               "SecretAuditBridge should map consumer_module into AuditContext.worker_type");
  assert_equal(std::string(kAuditContextUnknown), access_context.session_id,
               "SecretAuditBridge should keep unfrozen session_id on the audit context unknown default");
  assert_equal(std::string(kAuditContextUnknown), access_context.trace_id,
               "SecretAuditBridge should keep unfrozen trace_id on the audit context unknown default");

  assert_equal(std::string("secret.rotated"), logger->events[1].action,
               "SecretAuditBridge should map rotate emissions to the frozen secret.rotated action name");
  assert_equal(std::string("secret.revoked"), logger->events[2].action,
               "SecretAuditBridge should map revoke emissions to the frozen secret.revoked action name");
}

void test_secret_audit_bridge_maps_rejected_and_escalated_outcomes_and_accepts_degraded_success() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::secret::SecretAuditBridge;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = false,
      .error_code = std::nullopt,
  });
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = true,
      .error_code = std::nullopt,
  });

  SecretAuditBridge bridge(logger);

  const auto denied_result = bridge.emit_access_denied(
      make_secret_audit_event("policy_denied", "v3", false));
  const auto fallback_result = bridge.emit_fallback(
      make_secret_audit_event("backend_fallback", "v3", false));

  assert_true(denied_result.emitted && denied_result.is_valid(),
              "SecretAuditBridge should emit a valid rejected audit record for access_denied events");
  assert_true(fallback_result.emitted && fallback_result.is_valid() &&
                  fallback_result.write_outcome.is_degraded_success(),
              "SecretAuditBridge should accept degraded-success audit sink outcomes for fallback events");
  assert_true(logger->events[0].outcome == AuditOutcome::Rejected,
              "SecretAuditBridge should force AccessDenied events onto AuditOutcome::Rejected regardless of the boolean secret outcome flag");
  assert_true(logger->events[1].outcome == AuditOutcome::Escalated,
              "SecretAuditBridge should force Fallback events onto AuditOutcome::Escalated regardless of the boolean secret outcome flag");
}

void test_secret_audit_bridge_surfaces_audit_write_failures_with_secret_error_mapping() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = ResultCode::ProviderTimeout,
  });

  SecretAuditBridge bridge(logger);

  const auto result = bridge.emit_rotate(
      make_secret_audit_event("scheduled_rotation", "v4", true));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::ToolExecutionFailed,
              "SecretAuditBridge should map any non-success audit write outcome to INF_E_SECRET_AUDIT_WRITE_FAIL and keep the failure inside the frozen secret error domain");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find("INF_E_SECRET_AUDIT_WRITE_FAIL") != std::string::npos,
              "SecretAuditBridge should surface the frozen secret audit failure code in the emitted error payload");
  assert_equal(1, static_cast<int>(status.emit_failures),
               "SecretAuditBridge should track explicit audit emit failures for later diagnostics and health evaluation");
  assert_true(status.degraded && status.is_valid() &&
                  status.last_error_code == ResultCode::ToolExecutionFailed,
              "SecretAuditBridge should expose degraded status with the mapped secret audit failure result code after a sink write failure");
}

}  // namespace

int main() {
  try {
    test_secret_audit_bridge_emits_complete_access_rotate_and_revoke_events();
    test_secret_audit_bridge_maps_rejected_and_escalated_outcomes_and_accepts_degraded_success();
    test_secret_audit_bridge_surfaces_audit_write_failures_with_secret_error_mapping();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}