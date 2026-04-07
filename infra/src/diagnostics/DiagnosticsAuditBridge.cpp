#include "diagnostics/DiagnosticsAuditBridge.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include "diagnostics/DiagnosticsErrors.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kDiagnosticsAuditBridgeSourceRef =
    "DiagnosticsAuditBridge";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string normalize_actor_ref(std::string actor_ref) {
  if (actor_ref.rfind("actor://", 0) == 0 && !actor_ref.empty()) {
    return actor_ref;
  }

  return std::string("actor://redacted");
}

[[nodiscard]] std::string diagnostics_result_code_name(
    contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] AuditOutcome map_audit_outcome(
    DiagnosticsAuditEventOutcome outcome) {
  switch (outcome) {
    case DiagnosticsAuditEventOutcome::Success:
      return AuditOutcome::Succeeded;
    case DiagnosticsAuditEventOutcome::Failure:
      return AuditOutcome::Failed;
    case DiagnosticsAuditEventOutcome::Rejected:
      return AuditOutcome::Rejected;
  }

  return AuditOutcome::Unspecified;
}

[[nodiscard]] AuditWriteOutcome make_write_failure_outcome(
    contracts::ResultCode result_code) {
  return AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = result_code,
  };
}

void append_side_effect(std::vector<std::string>* side_effects,
                        std::string_view key,
                        std::string value) {
  if (!value.empty()) {
    side_effects->push_back(std::string(key) + ":" + std::move(value));
  }
}

[[nodiscard]] std::string audit_action_name(DiagnosticsAuditEventKind kind) {
  switch (kind) {
    case DiagnosticsAuditEventKind::RemoteExport:
      return "diagnostics.remote_export";
    case DiagnosticsAuditEventKind::CommandExtension:
      return "diagnostics.command_extension";
  }

  return "diagnostics.invalid_action";
}

[[nodiscard]] std::string audit_target(const DiagnosticsAuditEvent& event) {
  switch (event.kind) {
    case DiagnosticsAuditEventKind::RemoteExport:
      return "diagnostics.export:" + event.target_ref;
    case DiagnosticsAuditEventKind::CommandExtension:
      return "diagnostics.command:" + event.command_name;
  }

  return "diagnostics.unknown";
}

[[nodiscard]] std::string describe_write_failure(
    const AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "diagnostics audit sink returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "diagnostics audit sink returned a failing write outcome";
  }

  return "diagnostics audit sink did not report success or degraded success";
}

[[nodiscard]] DiagnosticsAuditEventOutcome remote_export_outcome_from_result(
    const SnapshotExportResult& result) {
  if (result.ok) {
    return DiagnosticsAuditEventOutcome::Success;
  }

  if (result.result_code ==
      map_diagnostics_error_code(DiagnosticsErrorCode::RemoteExportDisabled)
          .result_code) {
    return DiagnosticsAuditEventOutcome::Rejected;
  }

  return DiagnosticsAuditEventOutcome::Failure;
}

}  // namespace

DiagnosticsAuditBridge::DiagnosticsAuditBridge(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    DiagnosticsAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://diagnostics/audit/") +
                       "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://diagnostics/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "diagnostics-audit-event-");
}

void DiagnosticsAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

DiagnosticsAuditEmitResult DiagnosticsAuditBridge::emit_event(
    const DiagnosticsAuditEvent& event) {
  const std::string detail_ref = event.detail_ref.empty()
                                     ? options_.detail_ref_prefix + "invalid_event"
                                     : event.detail_ref;
  if (!event.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return DiagnosticsAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "diagnostics audit bridge requires a valid event payload before emit",
        "diagnostics.audit",
        std::string(kDiagnosticsAuditBridgeSourceRef));
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, detail_ref);
    return DiagnosticsAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::RuntimeRetryExhausted),
        contracts::ResultCode::RuntimeRetryExhausted,
        "diagnostics audit bridge requires an audit::IAuditLogger sink before audit-required actions can proceed",
        "diagnostics.audit",
        std::string(kDiagnosticsAuditBridgeSourceRef));
  }

  auto audit_event = make_audit_event(event);
  auto audit_context = make_audit_context(event);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return DiagnosticsAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "diagnostics audit bridge produced an invalid audit payload",
        "diagnostics.audit",
        std::string(kDiagnosticsAuditBridgeSourceRef));
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const auto result_code =
        write_outcome.error_code.value_or(contracts::ResultCode::RuntimeRetryExhausted);
    record_failure(result_code, detail_ref);
    return DiagnosticsAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        result_code,
        describe_write_failure(write_outcome),
        "diagnostics.audit",
        std::string(kDiagnosticsAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(detail_ref);
  return DiagnosticsAuditEmitResult::success(std::move(audit_event),
                                             std::move(audit_context),
                                             write_outcome);
}

DiagnosticsAuditEmitResult DiagnosticsAuditBridge::write_remote_export_event(
    const DiagnosticsSnapshot& snapshot,
    const SnapshotExportRequest& request,
    const SnapshotExportResult& result,
    DiagnosticsAuditContext context) {
  const auto detail_ref = result.ok
                              ? std::string("export://") + result.export_id
                              : (result.error.has_value()
                                     ? result.error->source_ref.ref_id
                                     : request.target_ref);
  return emit_event(DiagnosticsAuditEvent{
      .kind = DiagnosticsAuditEventKind::RemoteExport,
      .outcome = remote_export_outcome_from_result(result),
      .actor_ref = snapshot.command.actor_ref,
      .target_ref = request.target_ref,
      .command_name = {},
      .evidence_ref = std::string("snapshot://") + request.snapshot_id,
      .detail_ref = detail_ref,
      .request_scope = snapshot.command.request_scope,
      .format = request.format,
      .result_code = result.ok ? std::nullopt : std::optional<contracts::ResultCode>(result.result_code),
      .context = std::move(context),
      .timestamp_ms = current_time_unix_ms(),
  });
}

DiagnosticsAuditEmitResult DiagnosticsAuditBridge::write_command_extension_event(
    const DiagnosticsCommand& command,
    DiagnosticsAuditEventOutcome outcome,
    std::optional<contracts::ResultCode> result_code,
    std::string detail_ref,
    DiagnosticsAuditContext context) {
  return emit_event(DiagnosticsAuditEvent{
      .kind = DiagnosticsAuditEventKind::CommandExtension,
      .outcome = outcome,
      .actor_ref = command.actor_ref,
      .target_ref = {},
      .command_name = command.command_name,
      .evidence_ref = std::string("command://") + command.command_id,
      .detail_ref = detail_ref.empty()
                        ? std::string("command://") + command.command_id
                        : std::move(detail_ref),
      .request_scope = command.request_scope,
      .format = ExportFormat::Unspecified,
      .result_code = std::move(result_code),
      .context = std::move(context),
      .timestamp_ms = current_time_unix_ms(),
  });
}

DiagnosticsAuditBridgeStatus DiagnosticsAuditBridge::get_status() const {
  return DiagnosticsAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

AuditEvent DiagnosticsAuditBridge::make_audit_event(
    const DiagnosticsAuditEvent& event) {
  std::vector<std::string> side_effects;
  if (event.kind == DiagnosticsAuditEventKind::RemoteExport) {
    append_side_effect(&side_effects, "target_ref", event.target_ref);
    append_side_effect(&side_effects,
                       "format",
                       event.format == ExportFormat::Json ? std::string("json")
                                                          : std::string("unknown"));
  }
  append_side_effect(&side_effects,
                     "result_code",
                     event.result_code.has_value()
                         ? diagnostics_result_code_name(*event.result_code)
                         : std::string("none"));
  append_side_effect(&side_effects, "detail_ref", event.detail_ref);
  append_side_effect(&side_effects, "request_scope", event.request_scope);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = audit_action_name(event.kind),
      .actor = normalize_actor_ref(event.actor_ref),
      .target = audit_target(event),
      .outcome = map_audit_outcome(event.outcome),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = event.evidence_ref,
      },
      .side_effects = std::move(side_effects),
      .timestamp = event.timestamp_ms,
  };
}

AuditContext DiagnosticsAuditBridge::make_audit_context(
    const DiagnosticsAuditEvent& event) const {
  return AuditContext{
      .request_id = event.context.infra_context.request_id,
      .session_id = event.context.infra_context.session_id,
      .trace_id = event.context.infra_context.trace_id,
      .task_id = event.context.infra_context.task_id,
      .parent_task_id = event.context.infra_context.parent_task_id,
      .lease_id = event.context.infra_context.lease_id,
      .worker_type = event.context.worker_type,
  };
}

void DiagnosticsAuditBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void DiagnosticsAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::infra::diagnostics