#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::secret {

enum class SecretErrorCode {
  NotFound = 1,
  AccessDenied = 2,
  BackendUnavailable = 3,
  LeaseExpired = 4,
  VersionStale = 5,
  MaterializeFailed = 6,
  RotationValidationFailed = 7,
  RotationRollbackFailed = 8,
  AuditWriteFail = 9,
};

struct SecretErrorMapping {
  SecretErrorCode secret_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view secret_error_code_name(SecretErrorCode code) {
  switch (code) {
    case SecretErrorCode::NotFound:
      return "INF_E_SECRET_NOT_FOUND";
    case SecretErrorCode::AccessDenied:
      return "INF_E_SECRET_ACCESS_DENIED";
    case SecretErrorCode::BackendUnavailable:
      return "INF_E_SECRET_BACKEND_UNAVAILABLE";
    case SecretErrorCode::LeaseExpired:
      return "INF_E_SECRET_LEASE_EXPIRED";
    case SecretErrorCode::VersionStale:
      return "INF_E_SECRET_VERSION_STALE";
    case SecretErrorCode::MaterializeFailed:
      return "INF_E_SECRET_MATERIALIZE_FAILED";
    case SecretErrorCode::RotationValidationFailed:
      return "INF_E_SECRET_ROTATION_VALIDATION_FAILED";
    case SecretErrorCode::RotationRollbackFailed:
      return "INF_E_SECRET_ROTATION_ROLLBACK_FAILED";
    case SecretErrorCode::AuditWriteFail:
      return "INF_E_SECRET_AUDIT_WRITE_FAIL";
  }

  return "INF_E_SECRET_UNKNOWN";
}

inline constexpr SecretErrorMapping map_secret_error_code(SecretErrorCode code) {
  switch (code) {
    case SecretErrorCode::NotFound:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 get_secret",
          .reason = "missing secret records stay inside the contracts validation category until the caller corrects the secret identifier",
      };
    case SecretErrorCode::AccessDenied:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .source_anchor = "6.8 permission failure",
          .reason = "secret access denials stay inside the contracts policy failure category",
      };
    case SecretErrorCode::BackendUnavailable:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 backend failure",
          .reason = "secret backend unavailability stays inside the contracts provider failure category",
      };
    case SecretErrorCode::LeaseExpired:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 lifecycle failure",
          .reason = "expired secret leases stay inside the contracts runtime failure category",
      };
    case SecretErrorCode::VersionStale:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 stale handle",
          .reason = "stale secret versions stay inside the contracts runtime failure category until callers reacquire a fresh handle",
      };
    case SecretErrorCode::MaterializeFailed:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.6 materialize",
          .reason = "secret materialization failures stay inside the contracts technical execution failure category",
      };
    case SecretErrorCode::RotationValidationFailed:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.8 rotation validation",
          .reason = "secret rotation validation failures stay inside the contracts technical execution failure category",
      };
    case SecretErrorCode::RotationRollbackFailed:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 rotation rollback",
          .reason = "secret rollback exhaustion stays inside the contracts runtime failure category",
      };
    case SecretErrorCode::AuditWriteFail:
      return SecretErrorMapping{
          .secret_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.8 audit failure",
          .reason = "secret audit sink failures stay inside the contracts technical execution failure category",
      };
  }

  return SecretErrorMapping{
      .secret_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown secret errors fall back to the contracts runtime failure category",
  };
}

}  // namespace dasall::infra::secret