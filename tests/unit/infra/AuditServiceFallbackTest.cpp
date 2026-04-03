#include <exception>
#include <iostream>
#include <string>

#include "audit/AuditService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::AuditEvent make_event(std::string ref_suffix) {
  const auto event_id = std::string("audit-event-") + ref_suffix;

  return dasall::infra::AuditEvent{
      .event_id = event_id,
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("diagnostics.export"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::move(ref_suffix)},
      .side_effects = {"snapshot_exported"},
      .timestamp = 1711785603000,
  };
}

dasall::infra::AuditContext make_context() {
  return dasall::infra::AuditContext{};
}

dasall::infra::ExportQuery make_query() {
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

void test_audit_service_keeps_primary_pipeline_append_only_order() {
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;

  const auto init_result = service.init(AuditServiceConfig{.primary_capacity = 2, .fallback_capacity = 1});
  assert_true(init_result.ok,
              "audit service should initialize before append-only primary path testing");
  assert_true(service.start().ok,
              "audit service should start before append-only primary path testing");

  const auto first_write = service.write_audit(make_event("tool-call-append-001"), make_context());
  const auto second_write = service.write_audit(make_event("tool-call-append-002"), make_context());

  assert_true(first_write.is_success() && second_write.is_success(),
              "validated audit events should append to the primary pipeline until capacity is reached");
  assert_true(service.primary_record_count() == 2 && service.fallback_record_count() == 0,
              "append-only primary writes should not spill into fallback before the primary capacity is exhausted");
  assert_true(!service.is_degraded(),
              "primary append-only success should keep the service out of degraded mode");

  const auto export_result = service.export_audit(make_query());
  assert_true(export_result.records.size() == 2,
              "append-only primary writes should be exported as retained records");
  assert_true(export_result.records[0].event_id == "audit-event-tool-call-append-001" &&
                  export_result.records[1].event_id == "audit-event-tool-call-append-002",
              "primary pipeline should preserve append-only insertion order during export");
}

void test_audit_service_uses_fallback_when_primary_path_is_unavailable() {
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;

  const auto init_result = service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 2});
  assert_true(init_result.ok,
              "audit service should initialize with minimal primary and fallback capacity");
  assert_true(service.start().ok,
              "audit service should start after successful initialization");

  const auto primary_write = service.write_audit(make_event("tool-call-001"), make_context());
  assert_true(primary_write.is_success() && !primary_write.fallback_used,
              "first audit event should stay on the primary path");
  assert_true(service.primary_record_count() == 1,
              "primary pipeline should retain the first audit record");

  const auto fallback_write = service.write_audit(make_event("tool-call-002"), make_context());
  assert_true(fallback_write.is_degraded_success() && fallback_write.fallback_used,
              "second audit event should fall back when the primary path reaches capacity");
  assert_true(service.is_degraded(),
              "fallback use should mark the audit service as degraded");
  assert_true(service.fallback_record_count() == 1,
              "fallback pipeline should retain the degraded-path record");

  const auto export_result = service.export_audit(make_query());
  assert_true(export_result.records.size() == 2,
              "export should surface both primary and fallback audit records");
  assert_true(export_result.has_checksum() && export_result.is_complete_page(),
              "audit service export should return a checksum and explicit final-page semantics");
}

void test_audit_service_makes_fallback_exhaustion_observable() {
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  AuditService service;

  const auto init_result = service.init(AuditServiceConfig{.primary_capacity = 0, .fallback_capacity = 1});
  assert_true(init_result.ok,
              "audit service should allow a fallback-only degraded skeleton for failure-path testing");
  assert_true(service.start().ok,
              "audit service should start before fallback failure-path testing");

  const auto first_write = service.write_audit(make_event("tool-call-010"), make_context());
  assert_true(first_write.is_degraded_success() && first_write.fallback_used,
              "fallback-only configuration should persist the first record through fallback");

  const auto second_write = service.write_audit(make_event("tool-call-011"), make_context());
  assert_true(second_write.is_failure(),
              "audit service should surface an explicit failure when fallback capacity is exhausted");
  assert_true(second_write.fallback_used,
              "audit service should mark that fallback was attempted on exhaustion");
  assert_true(second_write.error_code == dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "audit fallback failures should remain observable through existing contracts runtime result codes");
}

}  // namespace

int main() {
  try {
    test_audit_service_keeps_primary_pipeline_append_only_order();
    test_audit_service_uses_fallback_when_primary_path_is_unavailable();
    test_audit_service_makes_fallback_exhaustion_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}