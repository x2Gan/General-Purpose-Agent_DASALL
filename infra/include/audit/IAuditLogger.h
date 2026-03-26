#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../AuditEvent.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::audit {

struct AuditExportFilter {
  std::string opaque_selector;

  [[nodiscard]] bool is_specified() const {
    return !opaque_selector.empty();
  }
};

struct AuditWriteResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  bool fallback_used = false;

  [[nodiscard]] static AuditWriteResult success(bool fallback_used = false) {
    return AuditWriteResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .fallback_used = fallback_used,
    };
  }

  [[nodiscard]] static AuditWriteResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref,
      bool fallback_used = false) {
    return AuditWriteResult{
        .ok = false,
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
                .ref_type = "infra.audit",
                .ref_id = std::move(source_ref),
            },
        },
        .fallback_used = fallback_used,
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

struct AuditExportResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  std::vector<AuditEvent> records;
  bool truncated = false;

  [[nodiscard]] static AuditExportResult success(
      std::vector<AuditEvent> records = {},
      bool truncated = false) {
    return AuditExportResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .records = std::move(records),
        .truncated = truncated,
    };
  }

  [[nodiscard]] static AuditExportResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return AuditExportResult{
        .ok = false,
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
                .ref_type = "infra.audit",
                .ref_id = std::move(source_ref),
            },
        },
        .records = {},
        .truncated = false,
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

class IAuditLogger {
 public:
  virtual ~IAuditLogger() = default;

  virtual AuditWriteResult write_audit(const AuditEvent& event) = 0;
  virtual AuditExportResult export_audit(const AuditExportFilter& filter) = 0;
};

}  // namespace dasall::infra::audit