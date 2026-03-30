#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::audit {

enum class AuditErrorCode {
  InvalidEvent = 1,
  WriteFail = 2,
  FallbackFail = 3,
  ExportDenied = 4,
  ExportFail = 5,
  RetentionFail = 6,
};

struct AuditErrorMapping {
  AuditErrorCode audit_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

inline constexpr std::string_view audit_error_code_name(AuditErrorCode code) {
  switch (code) {
    case AuditErrorCode::InvalidEvent:
      return "INF_E_AUDIT_INVALID_EVENT";
    case AuditErrorCode::WriteFail:
      return "INF_E_AUDIT_WRITE_FAIL";
    case AuditErrorCode::FallbackFail:
      return "INF_E_AUDIT_FALLBACK_FAIL";
    case AuditErrorCode::ExportDenied:
      return "INF_E_AUDIT_EXPORT_DENIED";
    case AuditErrorCode::ExportFail:
      return "INF_E_AUDIT_EXPORT_FAIL";
    case AuditErrorCode::RetentionFail:
      return "INF_E_AUDIT_RETENTION_FAIL";
  }

  return "INF_E_AUDIT_UNKNOWN";
}

inline constexpr AuditErrorMapping map_audit_error_code(AuditErrorCode code) {
  switch (code) {
    case AuditErrorCode::InvalidEvent:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "invalid audit input stays inside contracts validation category",
      };
    case AuditErrorCode::WriteFail:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "audit primary write failures stay inside contracts runtime category",
      };
    case AuditErrorCode::FallbackFail:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "audit fallback failures stay inside contracts runtime category",
      };
    case AuditErrorCode::ExportDenied:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "audit export denials stay inside contracts policy category",
      };
    case AuditErrorCode::ExportFail:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "audit export dependency failures stay inside contracts provider category",
      };
    case AuditErrorCode::RetentionFail:
      return AuditErrorMapping{
          .audit_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "audit retention failures stay inside contracts runtime category",
      };
  }

  return AuditErrorMapping{
      .audit_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown audit errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra::audit