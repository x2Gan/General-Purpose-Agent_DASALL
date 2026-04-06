#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class HealthErrorCode {
  ProbeTimeout = 1,
  ProbeException = 2,
  ProbeNotFound = 3,
  PolicyInvalid = 4,
  EventPublishFail = 5,
};

struct HealthErrorMapping {
  HealthErrorCode health_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view health_error_code_name(HealthErrorCode code) {
  switch (code) {
    case HealthErrorCode::ProbeTimeout:
      return "INF_E_HEALTH_PROBE_TIMEOUT";
    case HealthErrorCode::ProbeException:
      return "INF_E_HEALTH_PROBE_EXCEPTION";
    case HealthErrorCode::ProbeNotFound:
      return "INF_E_HEALTH_PROBE_NOT_FOUND";
    case HealthErrorCode::PolicyInvalid:
      return "INF_E_HEALTH_POLICY_INVALID";
    case HealthErrorCode::EventPublishFail:
      return "INF_E_HEALTH_EVENT_PUBLISH_FAIL";
  }

  return "INF_E_HEALTH_UNKNOWN";
}

inline constexpr HealthErrorMapping map_health_error_code(HealthErrorCode code) {
  switch (code) {
    case HealthErrorCode::ProbeTimeout:
      return HealthErrorMapping{
          .health_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 probe timeout",
          .reason = "health probe timeouts stay inside the contracts provider failure category",
      };
    case HealthErrorCode::ProbeException:
      return HealthErrorMapping{
          .health_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.8 probe exception",
          .reason = "health probe execution exceptions stay inside the contracts technical execution failure category",
      };
    case HealthErrorCode::ProbeNotFound:
      return HealthErrorMapping{
          .health_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 probe lookup",
          .reason = "missing health probe registrations stay inside the contracts validation category until callers correct the probe reference",
      };
    case HealthErrorCode::PolicyInvalid:
      return HealthErrorMapping{
          .health_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .source_anchor = "6.8 policy invalid",
          .reason = "invalid health policy configuration stays inside the contracts policy failure category",
      };
    case HealthErrorCode::EventPublishFail:
      return HealthErrorMapping{
          .health_code = code,
          .result_code = contracts::ResultCode::ToolExecutionFailed,
          .source_anchor = "6.8 event publish failure",
          .reason = "health event publish failures stay inside the contracts technical execution failure category",
      };
  }

  return HealthErrorMapping{
      .health_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown health errors fall back to the contracts runtime failure category",
  };
}

}  // namespace dasall::infra