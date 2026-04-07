#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "InfraContext.h"
#include "diagnostics/IDiagnosticsPolicyGuard.h"
#include "diagnostics/IDiagnosticsService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullDiagnosticsPolicyGuard final : public dasall::infra::diagnostics::IDiagnosticsPolicyGuard {
 public:
  [[nodiscard]] dasall::infra::diagnostics::CommandDecision authorize(
      const dasall::infra::diagnostics::DiagnosticsCommand& command,
      const dasall::infra::InfraContext& context) override {
    if (command.is_read_only_whitelisted() &&
        context.request_id != dasall::infra::InfraContext::kUnknownIdentifier) {
      return dasall::infra::diagnostics::CommandDecision{
          .allowed = true,
          .reason_code = std::string(),
          .policy_ref = std::string("policy://diagnostics/readonly"),
          .denied_rule_id = std::string(),
      };
    }

    return dasall::infra::diagnostics::CommandDecision{
        .allowed = false,
        .reason_code = std::string("diag_command_denied"),
        .policy_ref = std::string("policy://diagnostics/readonly"),
        .denied_rule_id = std::string("readonly-only"),
    };
  }
};

class NullDiagnosticsService final : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  [[nodiscard]] dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    if (!command.is_read_only_whitelisted()) {
      return dasall::infra::diagnostics::DiagnosticsSnapshotResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("diagnostics command must remain read-only and fully specified"),
          std::string("diagnostics.execute"),
          std::string("NullDiagnosticsService"),
          denied_decision());
    }

    return dasall::infra::diagnostics::DiagnosticsSnapshotResult::success(
        make_snapshot(command),
        allowed_decision());
  }

  [[nodiscard]] dasall::infra::diagnostics::DiagnosticsSnapshotResult get_snapshot(
      const dasall::infra::diagnostics::SnapshotQuery& query) override {
    if (!query.is_valid()) {
      return dasall::infra::diagnostics::DiagnosticsSnapshotResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("snapshot_id is required"),
          std::string("diagnostics.get_snapshot"),
          std::string("NullDiagnosticsService"));
    }

    auto snapshot = make_snapshot(valid_command());
    snapshot.snapshot_id = query.snapshot_id;
    return dasall::infra::diagnostics::DiagnosticsSnapshotResult::success(snapshot,
                                                                          allowed_decision());
  }

  [[nodiscard]] dasall::infra::diagnostics::SnapshotExportResult export_snapshot(
      const dasall::infra::diagnostics::SnapshotExportRequest& request) override {
    if (!request.is_valid()) {
      return dasall::infra::diagnostics::SnapshotExportResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("snapshot export target and format must be explicit"),
          std::string("diagnostics.export_snapshot"),
          std::string("NullDiagnosticsService"));
    }

    return dasall::infra::diagnostics::SnapshotExportResult::success(
        std::string("diag-export-001"),
        request.target,
        request.format,
        256,
        std::string("sha256:diag-export-001"),
        std::string("2026-04-07T10:00:00Z"));
  }

 public:
  [[nodiscard]] static dasall::infra::diagnostics::DiagnosticsCommand valid_command() {
    return dasall::infra::diagnostics::DiagnosticsCommand{
        .command_id = std::string("diag-cmd-interface-001"),
        .command_name = std::string("health.snapshot"),
        .args = {std::string("--summary")},
        .request_scope = std::string("runtime"),
        .timeout_ms = 3000,
        .actor_ref = std::string("ops-user"),
    };
  }

 private:
  [[nodiscard]] static dasall::infra::diagnostics::DiagnosticsSnapshot make_snapshot(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) {
    return dasall::infra::diagnostics::DiagnosticsSnapshot{
        .snapshot_id = std::string("diag-snapshot-001"),
        .command = command,
        .collected_at = std::string("2026-04-07T10:00:00Z"),
        .summary = std::string("diagnostics snapshot ready"),
        .evidence_refs = {std::string("logs://diagnostics/001")},
        .redaction_profile = dasall::infra::diagnostics::RedactionProfile::Strict,
        .exporter_hint = std::string("local_file"),
    };
  }

  [[nodiscard]] static dasall::infra::diagnostics::CommandDecision allowed_decision() {
    return dasall::infra::diagnostics::CommandDecision{
        .allowed = true,
        .reason_code = std::string(),
        .policy_ref = std::string("policy://diagnostics/readonly"),
        .denied_rule_id = std::string(),
    };
  }

  [[nodiscard]] static dasall::infra::diagnostics::CommandDecision denied_decision() {
    return dasall::infra::diagnostics::CommandDecision{
        .allowed = false,
        .reason_code = std::string("diag_command_denied"),
        .policy_ref = std::string("policy://diagnostics/readonly"),
        .denied_rule_id = std::string("readonly-only"),
    };
  }
};

void test_diagnostics_service_and_policy_guard_keep_frozen_entrypoints() {
  using dasall::infra::InfraContext;
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsSnapshotResult;
  using dasall::infra::diagnostics::IDiagnosticsPolicyGuard;
  using dasall::infra::diagnostics::IDiagnosticsService;
  using dasall::infra::diagnostics::SnapshotExportRequest;
  using dasall::infra::diagnostics::SnapshotExportResult;
  using dasall::infra::diagnostics::SnapshotQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IDiagnosticsService::execute),
                               DiagnosticsSnapshotResult (IDiagnosticsService::*)(
                                   const DiagnosticsCommand&)>);
  static_assert(std::is_same_v<decltype(&IDiagnosticsService::get_snapshot),
                               DiagnosticsSnapshotResult (IDiagnosticsService::*)(
                                   const SnapshotQuery&)>);
  static_assert(std::is_same_v<decltype(&IDiagnosticsService::export_snapshot),
                               SnapshotExportResult (IDiagnosticsService::*)(
                                   const SnapshotExportRequest&)>);
  static_assert(std::is_same_v<decltype(&IDiagnosticsPolicyGuard::authorize),
                               CommandDecision (IDiagnosticsPolicyGuard::*)(
                                   const DiagnosticsCommand&, const InfraContext&)>);
  static_assert(std::is_abstract_v<IDiagnosticsService>);
  static_assert(std::is_abstract_v<IDiagnosticsPolicyGuard>);

  NullDiagnosticsPolicyGuard policy_guard;
  const auto decision = policy_guard.authorize(
      NullDiagnosticsService::valid_command(),
      InfraContext{
          .request_id = std::string("req-diag-001"),
          .session_id = std::string("session-diag-001"),
          .trace_id = std::string("trace-diag-001"),
          .task_id = std::string("task-diag-001"),
          .parent_task_id = std::string("parent-diag-001"),
          .lease_id = std::string("lease-diag-001"),
      });
  assert_true(decision.allowed && decision.is_valid(),
              "IDiagnosticsPolicyGuard should keep authorize constrained to DiagnosticsCommand and InfraContext only");
}

void test_diagnostics_service_keeps_snapshot_and_export_boundaries_stable() {
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::SnapshotExportRequest;
  using dasall::infra::diagnostics::SnapshotQuery;
  using dasall::tests::support::assert_true;

  NullDiagnosticsService service;

  const auto execute_result = service.execute(NullDiagnosticsService::valid_command());
  assert_true(execute_result.ok && execute_result.snapshot.is_valid() &&
                  execute_result.decision.allowed,
              "IDiagnosticsService should keep execute returning DiagnosticsSnapshotResult over frozen snapshot and decision objects");

  const auto snapshot_result = service.get_snapshot(SnapshotQuery{
      .snapshot_id = std::string("diag-snapshot-lookup-001"),
  });
  assert_true(snapshot_result.ok && snapshot_result.snapshot.snapshot_id == "diag-snapshot-lookup-001",
              "IDiagnosticsService should keep get_snapshot constrained to SnapshotQuery and DiagnosticsSnapshotResult");

  const auto export_result = service.export_snapshot(SnapshotExportRequest{
      .snapshot_id = std::string("diag-snapshot-lookup-001"),
      .target = ExportTarget::LocalFile,
      .format = ExportFormat::Json,
      .target_ref = std::string("artifacts/diagnostics/diag-snapshot-lookup-001.json"),
  });
  assert_true(export_result.ok && export_result.is_valid(),
              "IDiagnosticsService should keep export_snapshot constrained to SnapshotExportRequest and SnapshotExportResult");
}

void test_diagnostics_service_and_policy_guard_failures_remain_observable() {
  using dasall::infra::InfraContext;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::tests::support::assert_true;

  NullDiagnosticsPolicyGuard policy_guard;
  NullDiagnosticsService service;

  const DiagnosticsCommand invalid_command{
      .command_id = std::string("diag-cmd-interface-002"),
      .command_name = std::string("process.kill"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };

  const auto denied = policy_guard.authorize(invalid_command, InfraContext{});
  assert_true(!denied.allowed && denied.is_valid() && denied.mapped_result_code().has_value(),
              "IDiagnosticsPolicyGuard deny path should remain traceable through CommandDecision only");

  const auto execute_failure = service.execute(invalid_command);
  assert_true(!execute_failure.ok && execute_failure.references_only_contract_error_types(),
              "IDiagnosticsService failure path should remain inside contracts ResultCode and ErrorInfo semantics");
}

}  // namespace

int main() {
  try {
    test_diagnostics_service_and_policy_guard_keep_frozen_entrypoints();
    test_diagnostics_service_keeps_snapshot_and_export_boundaries_stable();
    test_diagnostics_service_and_policy_guard_failures_remain_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}