#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "diagnostics/DiagnosticsErrors.h"
#include "diagnostics/DiagnosticsServiceFacade.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
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

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool contains_prefix(const std::vector<std::string>& refs, const std::string& prefix) {
  return std::any_of(refs.begin(), refs.end(), [&](const auto& ref) {
    return ref.rfind(prefix, 0) == 0;
  });
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

void test_diagnostics_integration_collects_reference_only_evidence_bundle() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics integration path should start the facade before execute");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("diag-int-001"),
      .command_name = std::string("queue.stats"),
      .args = {std::string("--queue=main")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });

  assert_true(execute_result.ok,
              "diagnostics integration path should execute queue.stats through the real facade pipeline");
  assert_true(execute_result.snapshot.is_valid(),
              "diagnostics integration path should produce a valid snapshot after evidence collection");
  assert_true(execute_result.snapshot.evidence_refs.size() >= 5,
              "evidence collection should expand snapshot evidence_refs beyond the executor-only minimum");
  assert_true(contains_prefix(execute_result.snapshot.evidence_refs, "logs://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "metrics://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "health://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "errors://"),
              "evidence collection should preserve logs/metrics/health/errors references as separate traceable summaries");
}

void test_diagnostics_integration_remote_export_writes_rejected_audit_event() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsErrorCode;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::infra::diagnostics::DiagnosticsServiceFacadeOptions;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  DiagnosticsServiceFacade service(DiagnosticsServiceFacadeOptions{
      .metrics_provider = nullptr,
      .audit_logger = logger,
  });
  assert_true(service.start(),
              "diagnostics integration remote export path should start the facade before issuing audit-required requests");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("diag-int-remote-001"),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });
  assert_true(execute_result.ok,
              "diagnostics integration remote export path should retain a redacted snapshot before export");

  const auto export_result = service.export_snapshot({
      .snapshot_id = execute_result.snapshot.snapshot_id,
      .target = ExportTarget::RemoteUpload,
      .format = ExportFormat::Json,
      .target_ref = std::string("https://diagnostics.example.test/upload"),
  });

  assert_true(!export_result.ok &&
                  export_result.result_code ==
                      dasall::infra::diagnostics::map_diagnostics_error_code(
                          DiagnosticsErrorCode::RemoteExportDisabled)
                          .result_code,
              "diagnostics integration remote export path should preserve the frozen remote-disabled rejection after the required audit sink persists the governance event");
  assert_true(logger->events.size() == 1 && logger->contexts.size() == 1,
              "diagnostics integration remote export path should persist exactly one audit event for the rejected remote export request");

  const auto& event = logger->events.back();
  const auto& context = logger->contexts.back();
  assert_true(event.action == "diagnostics.remote_export" &&
                  event.target == "diagnostics.export:https://diagnostics.example.test/upload" &&
                  event.outcome == dasall::infra::AuditOutcome::Rejected &&
                  event.actor == "actor://redacted" &&
                  event.evidence_ref.ref == std::string("snapshot://") + execute_result.snapshot.snapshot_id,
              "diagnostics integration remote export path should serialize the frozen audit action/target/outcome/actor/evidence contract");
  assert_true(has_side_effect(event, "target_ref:https://diagnostics.example.test/upload") &&
                  has_side_effect(event, "format:json") &&
                  has_side_effect(event, "result_code:PolicyDenied") &&
                  has_side_effect(event, "request_scope:runtime") &&
                  context.worker_type == "infra.diagnostics",
              "diagnostics integration remote export path should preserve only the frozen stable side effects and worker_type in the emitted audit payload");
}

void test_diagnostics_integration_remote_export_blocks_without_audit_sink() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics integration audit-block path should start the facade before remote export");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("diag-int-remote-002"),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });
  assert_true(execute_result.ok,
              "diagnostics integration audit-block path should retain a snapshot before testing remote export gate behavior");

  const auto export_result = service.export_snapshot({
      .snapshot_id = execute_result.snapshot.snapshot_id,
      .target = ExportTarget::RemoteUpload,
      .format = ExportFormat::Json,
      .target_ref = std::string("https://diagnostics.example.test/upload"),
  });

  assert_true(!export_result.ok && export_result.references_only_contract_error_types(),
              "diagnostics integration audit-block path should surface a contracts-aligned failure when the required audit sink is unavailable");
  assert_true(export_result.result_code == ResultCode::RuntimeRetryExhausted,
              "diagnostics integration audit-block path should normalize missing audit sinks to RuntimeRetryExhausted");
}

}  // namespace

int main() {
  try {
    test_diagnostics_integration_collects_reference_only_evidence_bundle();
    test_diagnostics_integration_remote_export_writes_rejected_audit_event();
    test_diagnostics_integration_remote_export_blocks_without_audit_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}