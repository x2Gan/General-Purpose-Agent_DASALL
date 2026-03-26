#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/RecoveryOutcomeGuards.h"
#include "tool/ToolResultGuards.h"
#include "../../../infra/include/AuditEvent.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_audit_event_accepts_tool_result_boundary_reference() {
  using dasall::contracts::ToolResult;
  using dasall::contracts::validate_tool_result_field_rules;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const ToolResult result{
      .request_id = std::string("req-001"),
      .tool_call_id = std::string("tool-call-001"),
      .tool_name = std::string("shell"),
      .success = true,
      .payload = std::string("{\"status\":\"ok\"}"),
      .error = std::nullopt,
      .side_effects = std::vector<std::string>{"wrote_file"},
      .completed_at = 12345,
      .duration_ms = 200,
      .goal_id = std::nullopt,
      .worker_task_id = std::string("task-001"),
      .tags = std::vector<std::string>{"audit"},
  };

  const auto result_guard = validate_tool_result_field_rules(result);
  assert_true(result_guard.ok,
              "ToolResult must pass its frozen contract guards before AuditEvent can reference it");

  const AuditEvent event{
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = *result.tool_call_id,
      },
      .outcome = AuditOutcome::Succeeded,
      .side_effects = {"wrote_file"},
  };

  assert_true(event.has_required_fields(),
              "AuditEvent should keep tool evidence as a plain reference, not an embedded contract object");
  assert_true(event.references_contract_outcome(),
              "AuditEvent should admit ToolResult references without expanding contract semantics");
}

void test_audit_event_accepts_recovery_outcome_boundary_reference() {
  using dasall::contracts::RecoveryOutcome;
  using dasall::contracts::validate_recovery_outcome_field_rules;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const RecoveryOutcome outcome{
      .executed_action = std::string("rollback"),
      .final_runtime_state = std::string("degraded"),
      .updated_retry_count = 1,
      .checkpoint_ref = std::string("checkpoint-001"),
      .compensation_result_ref = std::string("comp-001"),
      .rejection_reason = std::nullopt,
      .escalation_reason = std::string("manual approval required"),
  };

  const auto outcome_guard = validate_recovery_outcome_field_rules(outcome);
  assert_true(outcome_guard.ok,
              "RecoveryOutcome must pass its frozen contract guards before AuditEvent can reference it");

  const AuditEvent event{
      .action = std::string("ota.rollback"),
      .actor = std::string("recovery_manager"),
      .target = std::string("deployment-slot-a"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = *outcome.checkpoint_ref,
      },
      .outcome = AuditOutcome::Escalated,
      .side_effects = {"rollback_requested"},
  };

  assert_true(event.has_required_fields(),
              "AuditEvent should keep recovery evidence as a stable reference string");
  assert_true(event.references_contract_outcome(),
              "AuditEvent should admit RecoveryOutcome references without importing recovery control fields");
}

void test_audit_event_rejects_unspecified_evidence_boundary() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const AuditEvent event{
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .evidence_ref = {},
      .outcome = AuditOutcome::Failed,
      .side_effects = {"wrote_file"},
  };

  assert_true(!event.references_contract_outcome(),
              "unspecified evidence refs should fail the contract-boundary admission guard");
  assert_true(!event.has_required_fields(),
              "AuditEvent must not admit empty evidence refs for high-risk audit records");
}

}  // namespace

int main() {
  try {
    test_audit_event_accepts_tool_result_boundary_reference();
    test_audit_event_accepts_recovery_outcome_boundary_reference();
    test_audit_event_rejects_unspecified_evidence_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}