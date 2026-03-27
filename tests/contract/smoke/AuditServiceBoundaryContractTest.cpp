#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/audit/AuditService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::AuditEvent make_contract_aligned_event(std::string ref_suffix) {
  return dasall::infra::AuditEvent{
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("secret.rotate"),
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::move(ref_suffix)},
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .side_effects = {"secret_rotated"},
  };
}

void test_audit_service_keeps_audit_event_as_export_boundary() {
  using dasall::infra::AuditEvent;
  using dasall::infra::audit::AuditExportResult;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditExportResult{}.records), std::vector<AuditEvent>>);

  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit service should initialize for boundary verification");
  assert_true(service.start().ok,
              "audit service should start for boundary verification");
  assert_true(service.write_audit(make_contract_aligned_event("tool-call-100")).ok,
              "audit service should accept a contracts-aligned audit event");

  const auto export_result = service.export_audit({.opaque_selector = "all"});
  assert_true(export_result.ok,
              "audit service should export records without changing the boundary payload type");
  assert_true(export_result.records.size() == 1,
              "audit service export should surface the retained audit event");
  assert_true(export_result.records.front().references_contract_outcome(),
              "audit service export should preserve contracts-aligned evidence references");
}

void test_audit_service_rejects_non_contract_audit_evidence() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::audit::AuditWriteResult;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditWriteResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(AuditWriteResult{}.error), std::optional<ErrorInfo>>);

  AuditService service;
  assert_true(service.init(AuditServiceConfig{}).ok,
              "audit service should initialize before boundary rejection checks");
  assert_true(service.start().ok,
              "audit service should start before boundary rejection checks");

  const AuditEvent invalid_event{
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .evidence_ref = {.kind = AuditEvidenceKind::Unspecified, .ref = std::string("opaque")},
      .outcome = dasall::infra::AuditOutcome::Failed,
      .side_effects = {"command_denied"},
  };

  const auto write_result = service.write_audit(invalid_event);
  assert_true(!write_result.ok,
              "audit service should reject audit events that fall outside contracts evidence boundaries");
  assert_true(write_result.references_only_contract_error_types(),
              "audit service rejection path should stay inside contracts ResultCode/ErrorInfo types");
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