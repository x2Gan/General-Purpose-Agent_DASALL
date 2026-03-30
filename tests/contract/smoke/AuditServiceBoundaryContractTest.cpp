#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/audit/AuditService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::AuditEvent make_contract_aligned_event(std::string ref_suffix) {
  const auto event_id = std::string("audit-event-") + ref_suffix;

  return dasall::infra::AuditEvent{
      .event_id = event_id,
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("secret.rotate"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::move(ref_suffix)},
      .side_effects = {"secret_rotated"},
      .timestamp = 1711785604000,
  };
}

dasall::infra::AuditContext make_contract_context() {
  return dasall::infra::AuditContext{};
}

dasall::infra::ExportQuery make_contract_query() {
  return dasall::infra::ExportQuery{
      .start_ts = 1711785600000,
      .end_ts = 1711785605000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };
}

void test_audit_service_keeps_audit_event_as_export_boundary() {
  using dasall::infra::AuditEvent;
  using dasall::infra::ExportResult;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ExportResult{}.records), std::vector<AuditEvent>>);

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit service should initialize for boundary verification");
  assert_true(service.start().ok,
              "audit service should start for boundary verification");
  assert_true(service.write_audit(make_contract_aligned_event("tool-call-100"), make_contract_context()).is_success(),
              "audit service should accept a contracts-aligned audit event");

  const auto export_result = service.export_audit(make_contract_query());
  assert_true(export_result.records.size() == 1,
              "audit service export should surface the retained audit event");
  assert_true(export_result.records.front().references_contract_outcome(),
              "audit service export should preserve contracts-aligned evidence references");
  assert_true(export_result.has_checksum() && export_result.has_consistent_pagination(),
              "audit service export should keep checksum and pagination semantics inside the frozen ExportResult boundary");
}

void test_audit_service_rejects_non_contract_audit_evidence() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditWriteOutcome;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.error_code), std::optional<ResultCode>>);

  AuditService service;
  assert_true(service.init(AuditServiceConfig{}).ok,
              "audit service should initialize before boundary rejection checks");
  assert_true(service.start().ok,
              "audit service should start before boundary rejection checks");

  const AuditEvent invalid_event{
      .event_id = std::string("audit-event-invalid-001"),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .outcome = dasall::infra::AuditOutcome::Failed,
      .evidence_ref = {.kind = AuditEvidenceKind::Unspecified, .ref = std::string("opaque")},
      .side_effects = {"command_denied"},
      .timestamp = 1711785604100,
  };

  const auto write_result = service.write_audit(invalid_event, make_contract_context());
  assert_true(write_result.is_failure(),
              "audit service should reject audit events that fall outside contracts evidence boundaries");
  assert_true(write_result.error_code == ResultCode::ValidationFieldMissing,
              "audit service rejection path should stay inside existing contracts result codes");
}

}  // namespace

int main() {
  try {
    test_audit_service_keeps_audit_event_as_export_boundary();
    test_audit_service_rejects_non_contract_audit_evidence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}