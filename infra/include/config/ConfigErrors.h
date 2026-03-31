#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::config {

enum class ConfigErrorCode {
  NotFound = 1,
  TypeMismatch = 2,
  InvalidSchema = 3,
  Conflict = 4,
  SourceUnavailable = 5,
  SecretResolveFail = 6,
  ApplyRejected = 7,
  RollbackFailed = 8,
};

struct ConfigErrorMapping {
  ConfigErrorCode config_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

inline constexpr std::string_view config_error_code_name(ConfigErrorCode code) {
  switch (code) {
    case ConfigErrorCode::NotFound:
      return "INF_CFG_E_NOT_FOUND";
    case ConfigErrorCode::TypeMismatch:
      return "INF_CFG_E_TYPE_MISMATCH";
    case ConfigErrorCode::InvalidSchema:
      return "INF_CFG_E_INVALID_SCHEMA";
    case ConfigErrorCode::Conflict:
      return "INF_CFG_E_CONFLICT";
    case ConfigErrorCode::SourceUnavailable:
      return "INF_CFG_E_SOURCE_UNAVAILABLE";
    case ConfigErrorCode::SecretResolveFail:
      return "INF_CFG_E_SECRET_RESOLVE_FAIL";
    case ConfigErrorCode::ApplyRejected:
      return "INF_CFG_E_APPLY_REJECTED";
    case ConfigErrorCode::RollbackFailed:
      return "INF_CFG_E_ROLLBACK_FAILED";
  }

  return "INF_CFG_E_UNKNOWN";
}

inline constexpr ConfigErrorMapping map_config_error_code(ConfigErrorCode code) {
  switch (code) {
    case ConfigErrorCode::NotFound:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "missing config keys stay inside contracts validation category",
      };
    case ConfigErrorCode::TypeMismatch:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "typed config mismatches stay inside contracts validation category",
      };
    case ConfigErrorCode::InvalidSchema:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "schema validation failures stay inside contracts validation category",
      };
    case ConfigErrorCode::Conflict:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "conflicting config overlays stay inside contracts validation category",
      };
    case ConfigErrorCode::SourceUnavailable:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "config source dependency failures stay inside contracts provider category",
      };
    case ConfigErrorCode::SecretResolveFail:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "secret backend resolution failures stay inside contracts provider category",
      };
    case ConfigErrorCode::ApplyRejected:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "runtime patch policy rejections stay inside contracts policy category",
      };
    case ConfigErrorCode::RollbackFailed:
      return ConfigErrorMapping{
          .config_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "config rollback failures stay inside contracts runtime category",
      };
  }

  return ConfigErrorMapping{
      .config_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown config errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra::config