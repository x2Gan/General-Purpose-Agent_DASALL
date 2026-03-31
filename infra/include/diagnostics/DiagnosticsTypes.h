#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::diagnostics {

enum class RedactionProfile {
  Unspecified = 0,
  Strict = 1,
  Compatibility = 2,
};

enum class ExportTarget {
  Unspecified = 0,
  LocalFile = 1,
  RemoteUpload = 2,
};

enum class ExportFormat {
  Unspecified = 0,
  Json = 1,
  TextArchive = 2,
};

inline constexpr std::array<std::string_view, 3> kReadOnlyCommandWhitelist{
    "health.snapshot",
    "queue.stats",
    "thread.dump",
};

[[nodiscard]] inline bool is_read_only_command_whitelisted(std::string_view command_name) {
  return std::find(kReadOnlyCommandWhitelist.begin(),
                   kReadOnlyCommandWhitelist.end(),
                   command_name) != kReadOnlyCommandWhitelist.end();
}

struct DiagnosticsCommand {
  std::string command_id;
  std::string command_name;
  std::vector<std::string> args;
  std::string request_scope;
  std::uint32_t timeout_ms = 0;
  std::string actor_ref;

  [[nodiscard]] bool has_required_fields() const {
    return !command_id.empty() && !command_name.empty() && !request_scope.empty() &&
           timeout_ms > 0 && !actor_ref.empty();
  }

  [[nodiscard]] bool has_whitelisted_command_name() const {
    return is_read_only_command_whitelisted(command_name);
  }

  [[nodiscard]] bool is_read_only_whitelisted() const {
    return has_required_fields() && has_whitelisted_command_name();
  }
};

struct CommandDecision {
  bool allowed = false;
  std::string reason_code;
  std::string policy_ref;
  std::string denied_rule_id;

  [[nodiscard]] static std::optional<contracts::ResultCode> map_reason_code_to_result_code(
      std::string_view reason_code) {
    if (reason_code == "diag_command_denied") {
      return contracts::ResultCode::PolicyDenied;
    }

    if (reason_code == "diag_command_invalid") {
      return contracts::ResultCode::ValidationFieldMissing;
    }

    return std::nullopt;
  }

  [[nodiscard]] bool is_valid() const {
    if (allowed) {
      return denied_rule_id.empty() && (reason_code.empty() || !policy_ref.empty());
    }

    return !reason_code.empty() && !policy_ref.empty() &&
           map_reason_code_to_result_code(reason_code).has_value();
  }

  [[nodiscard]] std::optional<contracts::ResultCode> mapped_result_code() const {
    return map_reason_code_to_result_code(reason_code);
  }
};

struct EvidenceBundle {
  std::string logs_ref;
  std::string metrics_ref;
  std::string health_ref;
  std::string errors_ref;
  std::vector<std::string> artifacts;

  [[nodiscard]] bool is_valid() const {
    return !logs_ref.empty() && !metrics_ref.empty() && !health_ref.empty() &&
           !errors_ref.empty();
  }
};

struct DiagnosticsSnapshot {
  std::string snapshot_id;
  DiagnosticsCommand command;
  std::string collected_at;
  std::string summary;
  std::vector<std::string> evidence_refs;
  RedactionProfile redaction_profile = RedactionProfile::Unspecified;
  std::string exporter_hint;

  [[nodiscard]] bool is_redaction_ready() const {
    return command.is_read_only_whitelisted() && !collected_at.empty() && !summary.empty() &&
           !evidence_refs.empty() && redaction_profile != RedactionProfile::Unspecified;
  }

  [[nodiscard]] bool is_valid() const {
    return !snapshot_id.empty() && is_redaction_ready() && !exporter_hint.empty();
  }
};

struct SnapshotQuery {
  std::string snapshot_id;

  [[nodiscard]] bool is_valid() const {
    return !snapshot_id.empty();
  }
};

struct SnapshotExportRequest {
  std::string snapshot_id;
  ExportTarget target = ExportTarget::Unspecified;
  ExportFormat format = ExportFormat::Unspecified;
  std::string target_ref;

  [[nodiscard]] bool is_valid() const {
    return !snapshot_id.empty() && target != ExportTarget::Unspecified &&
           format != ExportFormat::Unspecified && !target_ref.empty();
  }
};

struct DiagnosticsSnapshotResult {
  bool ok = false;
  DiagnosticsSnapshot snapshot;
  CommandDecision decision;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static DiagnosticsSnapshotResult success(DiagnosticsSnapshot snapshot,
                                                         CommandDecision decision = {}) {
    return DiagnosticsSnapshotResult{
        .ok = true,
        .snapshot = std::move(snapshot),
        .decision = std::move(decision),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static DiagnosticsSnapshotResult failure(contracts::ResultCode result_code,
                                                         std::string message,
                                                         std::string stage,
                                                         std::string source_ref,
                                                         CommandDecision decision = {}) {
    return DiagnosticsSnapshotResult{
        .ok = false,
        .snapshot = {},
        .decision = std::move(decision),
        .result_code = result_code,
        .error = contracts::ErrorInfo{
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
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct SnapshotExportResult {
  bool ok = false;
  std::string export_id;
  ExportTarget target = ExportTarget::Unspecified;
  ExportFormat format = ExportFormat::Unspecified;
  std::uint64_t size_bytes = 0;
  std::string checksum;
  std::string created_at;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static SnapshotExportResult success(std::string export_id,
                                                    ExportTarget target,
                                                    ExportFormat format,
                                                    std::uint64_t size_bytes,
                                                    std::string checksum,
                                                    std::string created_at) {
    return SnapshotExportResult{
        .ok = true,
        .export_id = std::move(export_id),
        .target = target,
        .format = format,
        .size_bytes = size_bytes,
        .checksum = std::move(checksum),
        .created_at = std::move(created_at),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static SnapshotExportResult failure(contracts::ResultCode result_code,
                                                    std::string message,
                                                    std::string stage,
                                                    std::string source_ref) {
    return SnapshotExportResult{
        .ok = false,
        .export_id = {},
        .target = ExportTarget::Unspecified,
        .format = ExportFormat::Unspecified,
        .size_bytes = 0,
        .checksum = {},
        .created_at = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
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

  [[nodiscard]] bool is_valid() const {
    if (ok) {
      return !export_id.empty() && target != ExportTarget::Unspecified &&
             format != ExportFormat::Unspecified && size_bytes > 0 && !checksum.empty() &&
             !created_at.empty() && !error.has_value();
    }

    return error.has_value() && references_only_contract_error_types();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

}  // namespace dasall::infra::diagnostics