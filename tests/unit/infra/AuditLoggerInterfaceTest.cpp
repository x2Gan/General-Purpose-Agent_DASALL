#include <exception>
#include <iostream>
#include <string>

#include "audit/IAuditLogger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::audit::AuditWriteResult write_audit(
      const dasall::infra::AuditEvent& event) override {
    if (!event.has_required_fields() || !event.side_effects_are_serializable()) {
      return dasall::infra::audit::AuditWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "audit event must stay valid and serializable",
          "audit.write",
          "NullAuditLogger");
    }

    return dasall::infra::audit::AuditWriteResult::success();
  }

  dasall::infra::audit::AuditExportResult export_audit(
      const dasall::infra::audit::AuditExportFilter& filter) override {
    if (!filter.is_specified()) {
      return dasall::infra::audit::AuditExportResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "audit export filter must stay explicitly specified",
          "audit.export",
          "NullAuditLogger");
    }

    return dasall::infra::audit::AuditExportResult::success(
        {dasall::infra::AuditEvent{
            .action = std::string("tool.execute"),
            .actor = std::string("runtime"),
            .target = std::string("shell"),
            .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                             .ref = std::string("tool-call-001")},
            .outcome = dasall::infra::AuditOutcome::Succeeded,
            .side_effects = {"wrote_file"},
        }});
  }
};

void test_audit_logger_interface_accepts_audit_event_and_placeholder_export_filter() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::audit::AuditExportFilter;
  using dasall::tests::support::assert_true;

  NullAuditLogger logger;

  const AuditEvent event{
      .action = std::string("policy.patch"),
      .actor = std::string("runtime"),
      .target = std::string("policy-bundle-v2"),
      .evidence_ref = {.kind = AuditEvidenceKind::ToolResult,
                       .ref = std::string("tool-call-002")},
      .outcome = AuditOutcome::Succeeded,
      .side_effects = {"policy_reloaded"},
  };

  const auto write_result = logger.write_audit(event);
  assert_true(write_result.ok,
              "IAuditLogger skeleton should accept a valid AuditEvent after AuditEvent freeze");

  const auto export_result = logger.export_audit(AuditExportFilter{.opaque_selector = "last-hour"});
  assert_true(export_result.ok,
              "IAuditLogger skeleton should accept a specified placeholder export filter");
  assert_true(export_result.records.size() == 1,
              "placeholder export should prove that AuditEvent remains the export payload boundary");
}

void test_audit_logger_interface_reports_validation_failures_observably() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::infra::audit::AuditExportFilter;
  using dasall::tests::support::assert_true;

  NullAuditLogger logger;

  const AuditEvent invalid_event{
      .action = std::string(),
      .actor = std::string("runtime"),
      .target = std::string("deployment"),
      .evidence_ref = {},
      .outcome = AuditOutcome::Failed,
      .side_effects = {"rollback_requested"},
  };

  const auto write_result = logger.write_audit(invalid_event);
  assert_true(!write_result.ok,
              "IAuditLogger skeleton should reject incomplete audit events");
  assert_true(write_result.references_only_contract_error_types(),
              "audit write validation failures should stay within contracts error types");

  const auto export_result = logger.export_audit(AuditExportFilter{});
  assert_true(!export_result.ok,
              "IAuditLogger skeleton should reject an unspecified export filter placeholder");
  assert_true(export_result.references_only_contract_error_types(),
              "audit export validation failures should stay within contracts error types");
}

}  // namespace

int main() {
  try {
    test_audit_logger_interface_accepts_audit_event_and_placeholder_export_filter();
    test_audit_logger_interface_reports_validation_failures_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}