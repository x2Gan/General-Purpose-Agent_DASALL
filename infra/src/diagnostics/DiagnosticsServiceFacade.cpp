#include "diagnostics/DiagnosticsServiceFacade.h"

#include <chrono>
#include <string_view>
#include <utility>

#include "diagnostics/CommandExecutor.h"
#include "diagnostics/EvidenceCollector.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kDiagnosticsServiceFacadeSourceRef = "DiagnosticsServiceFacade";

[[nodiscard]] std::string current_time_rfc3339_stub() {
  return "2026-04-07T13:00:00Z";
}

[[nodiscard]] DiagnosticsSnapshotResult make_snapshot_failure(contracts::ResultCode result_code,
                                                             std::string message,
                                                             std::string stage) {
  return DiagnosticsSnapshotResult::failure(result_code,
                                            std::move(message),
                                            std::move(stage),
                                            std::string(kDiagnosticsServiceFacadeSourceRef));
}

[[nodiscard]] SnapshotExportResult make_export_failure(contracts::ResultCode result_code,
                                                       std::string message,
                                                       std::string stage) {
  return SnapshotExportResult::failure(result_code,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kDiagnosticsServiceFacadeSourceRef));
}

}  // namespace

DiagnosticsServiceFacade::DiagnosticsServiceFacade(DiagnosticsServiceFacadeOptions options)
    : options_(options) {}

bool DiagnosticsServiceFacade::start() {
  if (lifecycle_state_ == LifecycleState::Created) {
    lifecycle_state_ = LifecycleState::Ready;
  }

  return lifecycle_state_ == LifecycleState::Ready;
}

void DiagnosticsServiceFacade::enter_safe_mode_for_test(std::string reason) {
  lifecycle_state_ = LifecycleState::SafeMode;
  safe_mode_reason_ = reason.empty() ? std::optional<std::string>("diagnostics failure threshold reached")
                                     : std::optional<std::string>(std::move(reason));
}

bool DiagnosticsServiceFacade::is_ready() const {
  return lifecycle_state_ == LifecycleState::Ready;
}

bool DiagnosticsServiceFacade::is_in_safe_mode() const {
  return lifecycle_state_ == LifecycleState::SafeMode;
}

std::uint32_t DiagnosticsServiceFacade::consecutive_failures() const {
  return consecutive_failures_;
}

std::optional<std::string> DiagnosticsServiceFacade::safe_mode_reason() const {
  return safe_mode_reason_;
}

DiagnosticsSnapshotResult DiagnosticsServiceFacade::execute(const DiagnosticsCommand& command) {
  if (lifecycle_state_ == LifecycleState::Created) {
    note_failure("service_not_started");
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "diagnostics service must start before execute",
                                 "diagnostics.execute");
  }

  if (!command.is_read_only_whitelisted()) {
    note_failure("command_not_whitelisted");
    return DiagnosticsSnapshotResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        std::string("diagnostics command must remain read-only and whitelisted"),
        std::string("diagnostics.execute"),
        std::string(kDiagnosticsServiceFacadeSourceRef),
        CommandDecision{
            .allowed = false,
            .reason_code = std::string("diag_command_denied"),
            .policy_ref = std::string("policy://diagnostics/safe-mode"),
            .denied_rule_id = std::string("readonly-only"),
        });
  }

  if (!allows_command_in_current_mode(command)) {
    note_failure("safe_mode_restricted");
    return DiagnosticsSnapshotResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        std::string("diagnostics safe_mode only allows health.snapshot"),
        std::string("diagnostics.execute"),
        std::string(kDiagnosticsServiceFacadeSourceRef),
        CommandDecision{
            .allowed = false,
            .reason_code = std::string("diag_command_denied"),
            .policy_ref = std::string("policy://diagnostics/safe-mode"),
            .denied_rule_id = std::string("safe-mode-readonly-minimal"),
        });
  }

  const CommandExecutor executor;
  const auto execution = executor.execute(command);
  if (!execution.executed) {
    note_failure("command_execute_failed");
    return DiagnosticsSnapshotResult::failure(
        execution.result_code,
        execution.error.has_value() ? execution.error->details.message
                                    : std::string("diagnostics command execution failed"),
        std::string("diagnostics.execute"),
        std::string(kDiagnosticsServiceFacadeSourceRef),
        CommandDecision{
            .allowed = true,
            .reason_code = std::string(),
            .policy_ref = std::string("policy://diagnostics/readonly"),
            .denied_rule_id = std::string(),
        });
  }

  const EvidenceCollector collector;
  const auto evidence = collector.collect(command, execution);
  auto snapshot = build_snapshot(command);
  snapshot.collected_at = execution.executed_at;
  snapshot.summary = execution.summary;
  snapshot.evidence_refs = {evidence.logs_ref,
                            evidence.metrics_ref,
                            evidence.health_ref,
                            evidence.errors_ref};
  snapshot.evidence_refs.insert(snapshot.evidence_refs.end(),
                                evidence.artifacts.begin(),
                                evidence.artifacts.end());
  snapshots_[snapshot.snapshot_id] = snapshot;
  reset_failures();
  return DiagnosticsSnapshotResult::success(
      std::move(snapshot),
      CommandDecision{
          .allowed = true,
          .reason_code = {},
          .policy_ref = std::string("policy://diagnostics/readonly"),
          .denied_rule_id = {},
      });
}

DiagnosticsSnapshotResult DiagnosticsServiceFacade::get_snapshot(const SnapshotQuery& query) {
  if (lifecycle_state_ == LifecycleState::Created) {
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "diagnostics service must start before get_snapshot",
                                 "diagnostics.get_snapshot");
  }

  if (!query.is_valid()) {
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "snapshot_id is required",
                                 "diagnostics.get_snapshot");
  }

  const auto iterator = snapshots_.find(query.snapshot_id);
  if (iterator == snapshots_.end()) {
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "snapshot_id must resolve to a retained diagnostics snapshot",
                                 "diagnostics.get_snapshot");
  }

  return DiagnosticsSnapshotResult::success(iterator->second,
                                            CommandDecision{
                                                .allowed = true,
                                                .reason_code = {},
                                                .policy_ref = std::string("policy://diagnostics/readonly"),
                                                .denied_rule_id = {},
                                            });
}

SnapshotExportResult DiagnosticsServiceFacade::export_snapshot(
    const SnapshotExportRequest& request) {
  if (lifecycle_state_ == LifecycleState::Created) {
    return make_export_failure(contracts::ResultCode::ValidationFieldMissing,
                               "diagnostics service must start before export_snapshot",
                               "diagnostics.export_snapshot");
  }

  if (!request.is_valid()) {
    return make_export_failure(contracts::ResultCode::ValidationFieldMissing,
                               "diagnostics export must stay local and fully specified",
                               "diagnostics.export_snapshot");
  }

  if (request.target != ExportTarget::LocalFile) {
    return make_export_failure(contracts::ResultCode::ValidationFieldMissing,
                               "diagnostics export skeleton only allows local targets",
                               "diagnostics.export_snapshot");
  }

  if (!snapshots_.contains(request.snapshot_id)) {
    return make_export_failure(contracts::ResultCode::ValidationFieldMissing,
                               "diagnostics export requires an existing retained snapshot",
                               "diagnostics.export_snapshot");
  }

  return SnapshotExportResult::success(std::string("export-") + request.snapshot_id,
                                       request.target,
                                       request.format,
                                       256,
                                       std::string("sha256:diagnostics-export-001"),
                                       current_time_rfc3339_stub());
}

bool DiagnosticsServiceFacade::allows_command_in_current_mode(
    const DiagnosticsCommand& command) const {
  if (lifecycle_state_ != LifecycleState::SafeMode) {
    return true;
  }

  return command.command_name == "health.snapshot";
}

DiagnosticsSnapshot DiagnosticsServiceFacade::build_snapshot(const DiagnosticsCommand& command) {
  return DiagnosticsSnapshot{
      .snapshot_id = std::string("diag-snapshot-") + std::to_string(next_snapshot_index_++),
      .command = command,
      .collected_at = current_time_rfc3339_stub(),
  .summary = std::string("diagnostics executor skeleton snapshot"),
      .evidence_refs = {std::string("logs://diagnostics/facade"),
                        std::string("health://diagnostics/facade")},
      .redaction_profile = RedactionProfile::Strict,
      .exporter_hint = std::string("local_file"),
  };
}

void DiagnosticsServiceFacade::note_failure(std::string reason) {
  ++consecutive_failures_;
  if (consecutive_failures_ >= options_.safe_mode_failure_threshold) {
    lifecycle_state_ = LifecycleState::SafeMode;
    safe_mode_reason_ = reason.empty() ? std::optional<std::string>("diagnostics failure threshold reached")
                                       : std::optional<std::string>(std::move(reason));
  }
}

void DiagnosticsServiceFacade::reset_failures() {
  consecutive_failures_ = 0;
  if (lifecycle_state_ == LifecycleState::Ready) {
    safe_mode_reason_.reset();
  }
}

}  // namespace dasall::infra::diagnostics