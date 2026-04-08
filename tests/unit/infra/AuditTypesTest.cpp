#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "audit/AuditExporterTypes.h"
#include "audit/AuditTypes.h"
#include "audit/AuditValidator.h"
#include "logging/LogTypes.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::AuditEvent make_valid_audit_event() {
    return dasall::infra::AuditEvent{
            .event_id = std::string("audit-event-validator-001"),
            .action = std::string("tool.execute"),
            .actor = std::string("runtime"),
            .target = std::string("shell_tool"),
            .outcome = dasall::infra::AuditOutcome::Succeeded,
            .evidence_ref = {
                    .kind = dasall::infra::AuditEvidenceKind::ToolResult,
                    .ref = std::string("tool-call-validator-001"),
            },
            .side_effects = {"wrote_file"},
            .timestamp = 1711785600400,
    };
}

dasall::infra::AuditContext make_valid_audit_context() {
    return dasall::infra::AuditContext{
            .request_id = std::string("req-validator-001"),
            .session_id = std::string("session-validator-001"),
            .trace_id = std::string("trace-validator-001"),
            .task_id = std::string("task-validator-001"),
            .parent_task_id = std::string("parent-validator-001"),
            .lease_id = std::string("lease-validator-001"),
            .worker_type = std::string("tool-worker"),
    };
}

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

void test_audit_write_outcome_accepts_primary_and_fallback_success_paths() {
  using dasall::infra::AuditWriteOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.accepted), bool>);
  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.persisted), bool>);
  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.fallback_used), bool>);
  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.error_code),
                               std::optional<dasall::contracts::ResultCode>>);

  const AuditWriteOutcome primary_success{
      .accepted = true,
      .persisted = true,
      .fallback_used = false,
      .error_code = std::nullopt,
  };

  const AuditWriteOutcome fallback_success{
      .accepted = true,
      .persisted = true,
      .fallback_used = true,
      .error_code = std::nullopt,
  };

  assert_true(primary_success.is_success(),
              "accepted and persisted audit writes should be treated as primary success when fallback is not used");
  assert_true(fallback_success.is_degraded_success(),
              "accepted and persisted audit writes should be treated as degraded success when fallback is used");
}

void test_audit_write_outcome_accepts_observable_failure_mapping() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditWriteOutcome;
  using dasall::tests::support::assert_true;

  const AuditWriteOutcome failure{
      .accepted = true,
      .persisted = false,
      .fallback_used = true,
      .error_code = ResultCode::RuntimeRetryExhausted,
  };

  assert_true(failure.has_consistent_state(),
              "fallback write failures should remain consistent when they expose a mapped contracts result code");
  assert_true(failure.is_failure(),
              "non-persisted outcomes with mapped error codes should stay observable as failures");
}

void test_audit_write_outcome_rejects_inconsistent_combinations() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditWriteOutcome;
  using dasall::tests::support::assert_true;

  const AuditWriteOutcome persisted_without_acceptance{
      .accepted = false,
      .persisted = true,
      .fallback_used = false,
      .error_code = std::nullopt,
  };

  const AuditWriteOutcome persisted_with_error_code{
      .accepted = true,
      .persisted = true,
      .fallback_used = false,
      .error_code = ResultCode::ValidationFieldMissing,
  };

  assert_true(!persisted_without_acceptance.has_consistent_state(),
              "persisted outcomes must not bypass the accepted admission bit");
  assert_true(!persisted_with_error_code.has_consistent_state(),
              "persisted outcomes must not also carry an error code");
}

void test_audit_validator_accepts_well_formed_write_and_export_inputs() {
  using dasall::infra::ExportQuery;
  using dasall::infra::audit::AuditValidator;
  using dasall::tests::support::assert_true;

  const AuditValidator validator;
  const auto write_validation =
      validator.validate_write_input(make_valid_audit_event(), make_valid_audit_context());
  assert_true(write_validation.ok,
              "audit validator should admit well-formed audit event/context pairs");

  const auto export_validation = validator.validate_export_query(ExportQuery{
      .start_ts = 1711785600000,
      .end_ts = 1711785600900,
      .actor = std::string("runtime"),
      .action = std::string("tool.execute"),
      .target = std::string("shell_tool"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .page_token = std::string(),
  });
  assert_true(export_validation.ok,
              "audit validator should admit ordered export time windows");
}

void test_audit_validator_rejects_missing_fields_and_boundary_drift() {
  using dasall::infra::audit::AuditErrorCode;
  using dasall::infra::audit::AuditValidator;
  using dasall::tests::support::assert_true;

  const AuditValidator validator;

  auto missing_field_event = make_valid_audit_event();
  missing_field_event.event_id.clear();
  const auto missing_field_validation =
      validator.validate_write_input(missing_field_event, make_valid_audit_context());
  assert_true(!missing_field_validation.ok &&
                  missing_field_validation.error_code == AuditErrorCode::InvalidEvent,
              "audit validator should reject events that miss required fields");

  auto boundary_drift_event = make_valid_audit_event();
  boundary_drift_event.evidence_ref = {};
  const auto boundary_drift_validation =
      validator.validate_write_input(boundary_drift_event, make_valid_audit_context());
  assert_true(!boundary_drift_validation.ok &&
                  boundary_drift_validation.error_code == AuditErrorCode::InvalidEvent,
              "audit validator should reject audit events that fall outside the frozen contract evidence boundary");
}

void test_audit_validator_rejects_invalid_export_time_windows() {
  using dasall::infra::ExportQuery;
  using dasall::infra::audit::AuditErrorCode;
  using dasall::infra::audit::AuditValidator;
  using dasall::tests::support::assert_true;

  const AuditValidator validator;
  const auto invalid_window_validation = validator.validate_export_query(ExportQuery{
      .start_ts = 1711785600900,
      .end_ts = 1711785600000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  });

  assert_true(!invalid_window_validation.ok &&
                  invalid_window_validation.error_code == AuditErrorCode::InvalidEvent,
              "audit validator should reject export queries whose end timestamp precedes the start timestamp");
}

void test_logging_audit_aliases_preserve_required_fields_and_multi_agent_context() {
  using LoggingAuditContext = dasall::infra::logging::AuditContext;
  using LoggingAuditEvent = dasall::infra::logging::AuditEvent;
  using LoggingAuditEvidenceKind = dasall::infra::logging::AuditEvidenceKind;
  using LoggingAuditOutcome = dasall::infra::logging::AuditOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<LoggingAuditEvent, dasall::infra::AuditEvent>);
  static_assert(std::is_same_v<LoggingAuditContext, dasall::infra::AuditContext>);

  const LoggingAuditEvent event{
      .event_id = std::string("audit-event-logging-005"),
      .action = std::string("worker.dispatch"),
      .actor = std::string("multi_agent_coordinator"),
      .target = std::string("tool-worker"),
      .outcome = LoggingAuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = LoggingAuditEvidenceKind::WorkerTask,
          .ref = std::string("task-logging-005"),
      },
      .side_effects = {"worker_scheduled"},
      .timestamp = 1711785600300,
  };

  const LoggingAuditContext context{
      .request_id = std::string("req-005"),
      .session_id = std::string("session-005"),
      .trace_id = std::string("trace-005"),
      .task_id = std::string("task-logging-005"),
      .parent_task_id = std::string("parent-task-005"),
      .lease_id = std::string("lease-005"),
      .worker_type = std::string("tool-worker"),
  };

  assert_true(event.has_required_fields() && event.side_effects_are_serializable(),
              "logging::AuditEvent should preserve the required-field and side_effects guards from the frozen audit boundary");
  assert_true(context.has_non_empty_fields() && !context.uses_unknown_defaults(),
              "logging::AuditContext should preserve the explicit multi-agent correlation anchors required by ADR-008");
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
        test_audit_write_outcome_accepts_primary_and_fallback_success_paths();
        test_audit_write_outcome_accepts_observable_failure_mapping();
        test_audit_write_outcome_rejects_inconsistent_combinations();
                test_audit_validator_accepts_well_formed_write_and_export_inputs();
                test_audit_validator_rejects_missing_fields_and_boundary_drift();
                test_audit_validator_rejects_invalid_export_time_windows();
        test_logging_audit_aliases_preserve_required_fields_and_multi_agent_context();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}