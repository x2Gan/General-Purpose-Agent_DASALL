#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "InfraContext.h"
#include "audit/IAuditLogger.h"
#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

enum class DiagnosticsAuditEventKind {
  RemoteExport = 0,
  CommandExtension,
};

enum class DiagnosticsAuditEventOutcome {
  Success = 0,
  Failure,
  Rejected,
};

inline constexpr std::string_view kDiagnosticsAuditDefaultWorkerType =
    "infra.diagnostics";

[[nodiscard]] inline constexpr std::string_view diagnostics_audit_event_outcome_name(
    DiagnosticsAuditEventOutcome outcome) {
  switch (outcome) {
    case DiagnosticsAuditEventOutcome::Success:
      return "success";
    case DiagnosticsAuditEventOutcome::Failure:
      return "failure";
    case DiagnosticsAuditEventOutcome::Rejected:
      return "rejected";
  }

  return "unknown";
}

struct DiagnosticsAuditContext {
  InfraContext infra_context{};
  std::string worker_type = std::string(kDiagnosticsAuditDefaultWorkerType);

  [[nodiscard]] bool is_valid() const {
    return !infra_context.request_id.empty() && !infra_context.session_id.empty() &&
           !infra_context.trace_id.empty() && !infra_context.task_id.empty() &&
           !infra_context.parent_task_id.empty() && !infra_context.lease_id.empty() &&
           !worker_type.empty();
  }
};

struct DiagnosticsAuditEvent {
  DiagnosticsAuditEventKind kind = DiagnosticsAuditEventKind::RemoteExport;
  DiagnosticsAuditEventOutcome outcome = DiagnosticsAuditEventOutcome::Success;
  std::string actor_ref;
  std::string target_ref;
  std::string command_name;
  std::string evidence_ref;
  std::string detail_ref;
  std::string request_scope;
  ExportFormat format = ExportFormat::Unspecified;
  std::optional<contracts::ResultCode> result_code;
  DiagnosticsAuditContext context{};
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool is_valid() const {
    if (evidence_ref.empty() || detail_ref.empty() || request_scope.empty() ||
        timestamp_ms <= 0 || !context.is_valid()) {
      return false;
    }

    if (outcome == DiagnosticsAuditEventOutcome::Success && result_code.has_value()) {
      return false;
    }

    if (outcome != DiagnosticsAuditEventOutcome::Success && !result_code.has_value()) {
      return false;
    }

    switch (kind) {
      case DiagnosticsAuditEventKind::RemoteExport:
        return !target_ref.empty() && command_name.empty() &&
               format == ExportFormat::Json;
      case DiagnosticsAuditEventKind::CommandExtension:
        return target_ref.empty() && !command_name.empty() &&
               format == ExportFormat::Unspecified;
    }

    return false;
  }
};

struct DiagnosticsAuditBridgeOptions {
  std::string detail_ref_prefix = "status://diagnostics/audit/";
  std::string event_id_prefix = "diagnostics-audit-event-";
};

struct DiagnosticsAuditEmitResult {
  bool emitted = false;
  AuditEvent audit_event{};
  AuditContext audit_context{};
  AuditWriteOutcome write_outcome{};
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static DiagnosticsAuditEmitResult success(
      AuditEvent audit_event,
      AuditContext audit_context,
      AuditWriteOutcome write_outcome) {
    return DiagnosticsAuditEmitResult{
        .emitted = true,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static DiagnosticsAuditEmitResult failure(
      AuditEvent audit_event,
      AuditContext audit_context,
      AuditWriteOutcome write_outcome,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return DiagnosticsAuditEmitResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.diagnostics",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return emitted && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (emitted) {
      return audit_event.has_required_fields() &&
             audit_event.side_effects_are_serializable() &&
             audit_context.has_non_empty_fields() && !result_code.has_value() &&
             !error_info.has_value() &&
             (write_outcome.is_success() || write_outcome.is_degraded_success());
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct DiagnosticsAuditBridgeStatus {
  std::uint64_t emitted_total = 0;
  std::uint64_t emit_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value() &&
        contracts::classify_result_code(*last_error_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    return true;
  }
};

class DiagnosticsAuditBridge {
 public:
  explicit DiagnosticsAuditBridge(
      std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
      DiagnosticsAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] bool has_audit_logger() const {
    return static_cast<bool>(audit_logger_);
  }

  [[nodiscard]] DiagnosticsAuditEmitResult emit_event(
      const DiagnosticsAuditEvent& event);
  [[nodiscard]] DiagnosticsAuditEmitResult write_remote_export_event(
      const DiagnosticsSnapshot& snapshot,
      const SnapshotExportRequest& request,
      const SnapshotExportResult& result,
      DiagnosticsAuditContext context = {});
  [[nodiscard]] DiagnosticsAuditEmitResult write_command_extension_event(
      const DiagnosticsCommand& command,
      DiagnosticsAuditEventOutcome outcome,
      std::optional<contracts::ResultCode> result_code,
      std::string detail_ref,
      DiagnosticsAuditContext context = {});

  [[nodiscard]] DiagnosticsAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] AuditEvent make_audit_event(const DiagnosticsAuditEvent& event);
  [[nodiscard]] AuditContext make_audit_context(
      const DiagnosticsAuditEvent& event) const;

  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  DiagnosticsAuditBridgeOptions options_{};
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::diagnostics