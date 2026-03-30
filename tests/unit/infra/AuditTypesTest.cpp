#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "audit/AuditTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_audit_event_accepts_required_fields_and_contract_evidence_ref() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditEvent{}.event_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditEvent{}.timestamp), std::int64_t>);

  const AuditEvent event{
      .event_id = std::string("audit-event-001"),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell_tool"),
      .outcome = AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-001"),
      },
      .side_effects = {"wrote_file", "spawned_process"},
      .timestamp = 1711785600000,
  };

  assert_true(event.has_required_fields(),
              "audit event should require identity, who/what/where/outcome, evidence, and timestamp before admission");
  assert_true(event.references_contract_boundary(),
              "audit evidence should stay anchored to frozen contract boundary kinds");
  assert_true(event.side_effects_are_serializable(),
              "string side effects should stay serializable at L3 type freeze");
}

void test_audit_event_rejects_missing_required_fields() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::tests::support::assert_true;

  const AuditEvent missing_event_id{
      .event_id = std::string(),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell_tool"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-001"),
      },
      .side_effects = {},
      .timestamp = 1711785600000,
  };

  const AuditEvent invalid_timestamp{
      .event_id = std::string("audit-event-002"),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell_tool"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-002"),
      },
      .side_effects = {},
      .timestamp = 0,
  };

  assert_true(!missing_event_id.has_required_fields(),
              "missing event_id should fail audit required-field validation");
  assert_true(!invalid_timestamp.has_required_fields(),
              "non-positive timestamp should fail audit required-field validation");
}

void test_audit_event_rejects_empty_or_duplicate_side_effects() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const AuditEvent duplicate_side_effects{
      .event_id = std::string("audit-event-003"),
      .action = std::string("ota.apply"),
      .actor = std::string("operator"),
      .target = std::string("device-profile"),
      .outcome = AuditOutcome::Escalated,
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("checkpoint-001"),
      },
      .side_effects = {"restarted_service", "restarted_service"},
      .timestamp = 1711785600100,
  };

  const AuditEvent empty_side_effect{
      .event_id = std::string("audit-event-004"),
      .action = std::string("ota.apply"),
      .actor = std::string("operator"),
      .target = std::string("device-profile"),
      .outcome = AuditOutcome::Failed,
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = std::string("task-001"),
      },
      .side_effects = {""},
      .timestamp = 1711785600200,
  };

  assert_true(!duplicate_side_effects.side_effects_are_serializable(),
              "duplicate side_effects should be rejected by the minimal serializable guard");
  assert_true(!empty_side_effect.side_effects_are_serializable(),
              "empty side_effects should be rejected by the minimal serializable guard");
}

void test_audit_context_defaults_missing_identifiers_to_unknown() {
  using dasall::infra::AuditContext;
  using dasall::infra::kAuditContextUnknown;
  using dasall::tests::support::assert_true;

  const AuditContext context{};

  assert_true(context.uses_unknown_defaults(),
              "audit context should default missing correlation identifiers to unknown instead of null semantics");
  assert_true(context.has_non_empty_fields(),
              "audit context should keep all correlation anchors non-empty after default construction");
  assert_true(context.request_id == kAuditContextUnknown &&
                  context.session_id == kAuditContextUnknown &&
                  context.trace_id == kAuditContextUnknown,
              "request/session/trace identifiers should use the frozen unknown placeholder when absent");
}

void test_audit_context_preserves_supplied_correlation_identifiers() {
  using dasall::infra::AuditContext;
  using dasall::tests::support::assert_true;

  const AuditContext context{
      .request_id = std::string("req-001"),
      .session_id = std::string("session-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string("task-001"),
      .parent_task_id = std::string("parent-task-001"),
      .lease_id = std::string("lease-001"),
      .worker_type = std::string("tool-worker"),
  };

  assert_true(context.has_non_empty_fields(),
              "explicitly supplied audit context identifiers should remain non-empty");
  assert_true(!context.uses_unknown_defaults(),
              "explicit identifiers should not collapse back to the unknown placeholder set");
}

void test_audit_context_rejects_empty_strings_when_callers_bypass_unknown_defaults() {
  using dasall::infra::AuditContext;
  using dasall::tests::support::assert_true;

  const AuditContext invalid_context{
      .request_id = std::string(),
      .session_id = std::string("session-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string("task-001"),
      .parent_task_id = std::string("parent-task-001"),
      .lease_id = std::string("lease-001"),
      .worker_type = std::string("tool-worker"),
  };

  assert_true(!invalid_context.has_non_empty_fields(),
              "callers that bypass unknown defaults with empty strings should fail the non-empty guard");
}

}  // namespace

int main() {
  try {
    test_audit_event_accepts_required_fields_and_contract_evidence_ref();
    test_audit_event_rejects_missing_required_fields();
    test_audit_event_rejects_empty_or_duplicate_side_effects();
        test_audit_context_defaults_missing_identifiers_to_unknown();
        test_audit_context_preserves_supplied_correlation_identifiers();
        test_audit_context_rejects_empty_strings_when_callers_bypass_unknown_defaults();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}