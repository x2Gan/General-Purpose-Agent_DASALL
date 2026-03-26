#include <exception>
#include <iostream>
#include <string>

#include "AuditEvent.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_audit_event_accepts_required_fields_and_contract_evidence_ref() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const AuditEvent event{
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell_tool"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-001"),
      },
      .outcome = AuditOutcome::Succeeded,
      .side_effects = {"wrote_file", "spawned_process"},
  };

  assert_true(event.has_required_fields(),
              "audit event should require who/what/target/evidence/outcome before admission");
  assert_true(event.references_contract_outcome(),
              "audit evidence should stay anchored to frozen contract outcome kinds");
  assert_true(event.side_effects_are_serializable(),
              "string side effects should stay serializable at L2 freeze");
}

void test_audit_event_rejects_missing_required_fields() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::tests::support::assert_true;

  const AuditEvent event{
      .action = std::string("tool.execute"),
      .actor = std::string(),
      .target = std::string("shell_tool"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-001"),
      },
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .side_effects = {},
  };

  assert_true(!event.has_required_fields(),
              "missing actor or unspecified outcome should fail audit required-field validation");
}

void test_audit_event_rejects_empty_or_duplicate_side_effects() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const AuditEvent duplicate_side_effects{
      .action = std::string("ota.apply"),
      .actor = std::string("operator"),
      .target = std::string("device-profile"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("checkpoint-001"),
      },
      .outcome = AuditOutcome::Escalated,
      .side_effects = {"restarted_service", "restarted_service"},
  };

  const AuditEvent empty_side_effect{
      .action = std::string("ota.apply"),
      .actor = std::string("operator"),
      .target = std::string("device-profile"),
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("checkpoint-002"),
      },
      .outcome = AuditOutcome::Failed,
      .side_effects = {""},
  };

  assert_true(!duplicate_side_effects.side_effects_are_serializable(),
              "duplicate side_effects should be rejected by the minimal serializable guard");
  assert_true(!empty_side_effect.side_effects_are_serializable(),
              "empty side_effects should be rejected by the minimal serializable guard");
}

}  // namespace

int main() {
  try {
    test_audit_event_accepts_required_fields_and_contract_evidence_ref();
    test_audit_event_rejects_missing_required_fields();
    test_audit_event_rejects_empty_or_duplicate_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}