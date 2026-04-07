#include "InfraErrorCode.h"

namespace dasall::infra {

std::string_view infra_error_code_name(InfraErrorCode code) {
  switch (code) {
    case InfraErrorCode::ConfigInvalid:
      return "INF_E_CONFIG_INVALID";
    case InfraErrorCode::SecretUnavailable:
      return "INF_E_SECRET_UNAVAILABLE";
    case InfraErrorCode::LogQueueFull:
      return "INF_E_LOG_QUEUE_FULL";
    case InfraErrorCode::AuditWriteFail:
      return "INF_E_AUDIT_WRITE_FAIL";
    case InfraErrorCode::HealthProbeTimeout:
      return "INF_E_HEALTH_PROBE_TIMEOUT";
    case InfraErrorCode::OTAVerifyFail:
      return "INF_E_OTA_VERIFY_FAIL";
    case InfraErrorCode::OTARollbackFail:
      return "INF_E_OTA_ROLLBACK_FAIL";
    case InfraErrorCode::OTABootConfirmTimeout:
      return "INF_E_OTA_BOOT_CONFIRM_TIMEOUT";
  }

  return "INF_E_UNKNOWN";
}

InfraErrorMapping map_infra_error_code(InfraErrorCode code) {
  switch (code) {
    case InfraErrorCode::ConfigInvalid:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "invalid infra config input stays inside contracts validation category",
      };
    case InfraErrorCode::SecretUnavailable:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "secret backend unavailability stays inside contracts provider category",
      };
    case InfraErrorCode::LogQueueFull:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "log queue saturation stays inside contracts runtime category",
      };
    case InfraErrorCode::AuditWriteFail:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "audit write failure stays inside contracts runtime category",
      };
    case InfraErrorCode::HealthProbeTimeout:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "probe timeout stays inside contracts provider timeout category",
      };
    case InfraErrorCode::OTAVerifyFail:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "OTA verification failure stays inside contracts validation category",
      };
    case InfraErrorCode::OTARollbackFail:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "OTA rollback failure stays inside contracts runtime category",
      };
    case InfraErrorCode::OTABootConfirmTimeout:
      return InfraErrorMapping{
          .infra_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "OTA boot confirm timeout stays inside contracts provider timeout category",
      };
  }

  return InfraErrorMapping{
      .infra_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown infra errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra