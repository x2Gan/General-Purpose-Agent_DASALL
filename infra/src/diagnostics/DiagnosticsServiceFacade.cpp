#include "diagnostics/DiagnosticsServiceFacade.h"

#include <string_view>
#include <utility>

#include "diagnostics/CommandExecutor.h"
#include "diagnostics/DiagnosticsErrors.h"
#include "diagnostics/EvidenceCollector.h"
#include "diagnostics/ExportManager.h"
#include "diagnostics/RedactionEngine.h"

#include <chrono>

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kDiagnosticsServiceFacadeSourceRef = "DiagnosticsServiceFacade";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string execute_stage_for_command(
    const std::string_view& command_name) {
  if (command_name == "health.snapshot") {
    return "execute.health_snapshot";
  }

  if (command_name == "queue.stats") {
    return "execute.queue_stats";
  }

  return "execute.thread_dump";
}

[[nodiscard]] std::string export_stage_for_target(ExportTarget target) {
  switch (target) {
    case ExportTarget::RemoteUpload:
      return "export.remote_upload";
    case ExportTarget::LocalFile:
    case ExportTarget::Unspecified:
      return "export.local_file";
  }

  return "export.local_file";
}

[[nodiscard]] std::string diagnostics_error_label(DiagnosticsErrorCode code) {
  return std::string(diagnostics_error_code_name(code));
}

[[nodiscard]] std::string execution_error_label(contracts::ResultCode result_code) {
  if (result_code ==
      map_diagnostics_error_code(DiagnosticsErrorCode::ExecTimeout).result_code) {
    return diagnostics_error_label(DiagnosticsErrorCode::ExecTimeout);
  }

  return diagnostics_error_label(DiagnosticsErrorCode::ExecFail);
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
    : options_(std::move(options)),
      snapshot_store_(SnapshotStoreOptions{.retention_days = options.snapshot_retention_days,
                                           .max_snapshot_count = options.snapshot_max_count}),
  audit_bridge_(options_.audit_logger),
      metrics_bridge_(options_.metrics_provider, options_.profile_id) {}

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

void DiagnosticsServiceFacade::inject_snapshot_store_commit_failure_for_test(std::string reason) {
  snapshot_store_.inject_commit_failure_for_test(std::move(reason));
}

DiagnosticsSnapshotResult DiagnosticsServiceFacade::execute(const DiagnosticsCommand& command) {
  if (lifecycle_state_ == LifecycleState::Created) {
    note_failure("service_not_started");
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "diagnostics service must start before execute",
                                 "diagnostics.execute");
  }

  if (!command.is_read_only_whitelisted()) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
        .kind = DiagnosticsMetricKind::CommandDeniedTotal,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = std::string("authorize"),
        .outcome = std::string("rejected"),
        .error_code = std::string(kDiagnosticsMetricDeniedReasonLabel),
    });
    if (command.has_whitelisted_command_name()) {
      (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
          .kind = DiagnosticsMetricKind::CommandTotal,
          .value = 1.0,
          .ts_unix_ms = current_time_unix_ms(),
          .stage = execute_stage_for_command(command.command_name),
          .outcome = std::string("rejected"),
          .error_code = std::string(kDiagnosticsMetricDeniedReasonLabel),
      });
    }
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
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandDeniedTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = std::string("authorize"),
      .outcome = std::string("rejected"),
      .error_code = std::string(kDiagnosticsMetricDeniedReasonLabel),
    });
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("rejected"),
      .error_code = std::string(kDiagnosticsMetricDeniedReasonLabel),
    });
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
    const std::string error_code = execution_error_label(execution.result_code);
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("failure"),
      .error_code = error_code,
    });
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::ExecLatencyMs,
      .value = static_cast<double>(execution.latency_ms),
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("failure"),
      .error_code = error_code,
    });
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

  (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::ExecLatencyMs,
      .value = static_cast<double>(execution.latency_ms),
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("success"),
      .error_code = std::string(kDiagnosticsMetricNoErrorCodeLabel),
  });

  const EvidenceCollector collector;
  const auto evidence = collector.collect(command, execution);
  auto snapshot = snapshot_assembler_.assemble(command, execution, evidence);
  const RedactionEngine redaction_engine;
  const auto redaction = redaction_engine.redact(std::move(snapshot));
  if (!redaction.redacted) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::RedactionFailTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = std::string("redaction"),
      .outcome = std::string("failure"),
      .error_code = diagnostics_error_label(DiagnosticsErrorCode::RedactionFail),
    });
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("failure"),
      .error_code = diagnostics_error_label(DiagnosticsErrorCode::RedactionFail),
    });
    note_failure("redaction_failed");
    return DiagnosticsSnapshotResult::failure(
        redaction.result_code,
        redaction.error.has_value() ? redaction.error->details.message
                                    : std::string("diagnostics redaction failed"),
        std::string("diagnostics.redact"),
        std::string(kDiagnosticsServiceFacadeSourceRef),
        CommandDecision{
            .allowed = true,
            .reason_code = std::string(),
            .policy_ref = std::string("policy://diagnostics/readonly"),
            .denied_rule_id = std::string(),
        });
  }

  snapshot = std::move(redaction.snapshot);
  const auto store_result = snapshot_store_.store(snapshot);
  if (!store_result.stored) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::SnapshotStoreFailTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = std::string("store"),
      .outcome = std::string("failure"),
      .error_code = diagnostics_error_label(DiagnosticsErrorCode::SnapshotStoreFail),
    });
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("failure"),
      .error_code = diagnostics_error_label(DiagnosticsErrorCode::SnapshotStoreFail),
    });
    note_failure("snapshot_store_failed");
    return DiagnosticsSnapshotResult::failure(
        store_result.result_code,
        store_result.error.has_value() ? store_result.error->details.message
                                       : std::string("diagnostics snapshot persistence failed"),
        store_result.error.has_value() ? store_result.error->details.stage
                                       : std::string("diagnostics.store_snapshot"),
        std::string(kDiagnosticsServiceFacadeSourceRef),
        CommandDecision{
            .allowed = true,
            .reason_code = std::string(),
            .policy_ref = std::string("policy://diagnostics/readonly"),
            .denied_rule_id = std::string(),
        });
  }

  (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = execute_stage_for_command(command.command_name),
      .outcome = std::string("success"),
      .error_code = std::string(kDiagnosticsMetricNoErrorCodeLabel),
  });

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

  const auto snapshot = snapshot_store_.get(query.snapshot_id);
  if (!snapshot.has_value()) {
    return make_snapshot_failure(contracts::ResultCode::ValidationFieldMissing,
                                 "snapshot_id must resolve to a retained diagnostics snapshot",
                                 "diagnostics.get_snapshot");
  }

  return DiagnosticsSnapshotResult::success(*snapshot,
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

  const auto snapshot = snapshot_store_.get(request.snapshot_id);
  if (!snapshot.has_value()) {
    return make_export_failure(contracts::ResultCode::ValidationFieldMissing,
                               "diagnostics export requires an existing retained snapshot",
                               "diagnostics.export_snapshot");
  }

  const ExportManager export_manager;
  auto result = export_manager.export_snapshot(*snapshot, request);
  const auto stage = export_stage_for_target(request.target);
  if (request.target == ExportTarget::RemoteUpload) {
    const auto audit_result = audit_bridge_.write_remote_export_event(*snapshot, request, result);
    if (!audit_result.emitted) {
      result = SnapshotExportResult::failure(
          audit_result.result_code.value_or(contracts::ResultCode::RuntimeRetryExhausted),
          audit_result.error_info.has_value()
              ? audit_result.error_info->details.message
              : std::string("diagnostics audit bridge blocked remote export"),
          audit_result.error_info.has_value()
              ? audit_result.error_info->details.stage
              : std::string("diagnostics.audit_remote_export"),
          audit_result.error_info.has_value()
              ? audit_result.error_info->source_ref.ref_id
              : std::string(kDiagnosticsServiceFacadeSourceRef));
    }
  }

  if (result.ok) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
        .kind = DiagnosticsMetricKind::ExportTotal,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = stage,
        .outcome = std::string("success"),
        .error_code = std::string(kDiagnosticsMetricNoErrorCodeLabel),
    });
  } else if (request.target == ExportTarget::RemoteUpload &&
             result.result_code ==
                 map_diagnostics_error_code(DiagnosticsErrorCode::RemoteExportDisabled)
                     .result_code) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
        .kind = DiagnosticsMetricKind::ExportTotal,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = stage,
        .outcome = std::string("rejected"),
        .error_code = diagnostics_error_label(
            DiagnosticsErrorCode::RemoteExportDisabled),
    });
  } else {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
        .kind = DiagnosticsMetricKind::ExportTotal,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = stage,
        .outcome = std::string("failure"),
        .error_code = diagnostics_error_label(DiagnosticsErrorCode::ExportFail),
    });
  }

  return result;
}

bool DiagnosticsServiceFacade::allows_command_in_current_mode(
    const DiagnosticsCommand& command) const {
  if (lifecycle_state_ != LifecycleState::SafeMode) {
    return true;
  }

  return command.command_name == "health.snapshot";
}

void DiagnosticsServiceFacade::note_failure(std::string reason) {
  const bool entering_safe_mode =
      lifecycle_state_ != LifecycleState::SafeMode &&
      consecutive_failures_ + 1 >= options_.safe_mode_failure_threshold;
  ++consecutive_failures_;
  if (consecutive_failures_ >= options_.safe_mode_failure_threshold) {
    lifecycle_state_ = LifecycleState::SafeMode;
    safe_mode_reason_ = reason.empty() ? std::optional<std::string>("diagnostics failure threshold reached")
                                       : std::optional<std::string>(std::move(reason));
  }

  if (entering_safe_mode) {
    (void)metrics_bridge_.emit(DiagnosticsMetricSignal{
        .kind = DiagnosticsMetricKind::SafeModeEnterTotal,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = std::string("safe_mode"),
        .outcome = std::string("degraded"),
        .error_code = std::string(kDiagnosticsMetricNoErrorCodeLabel),
    });
  }
}

void DiagnosticsServiceFacade::reset_failures() {
  consecutive_failures_ = 0;
  if (lifecycle_state_ == LifecycleState::Ready) {
    safe_mode_reason_.reset();
  }
}

}  // namespace dasall::infra::diagnostics