#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class InfraErrorCode {
  ConfigInvalid = 1,
  SecretUnavailable = 2,
  LogQueueFull = 3,
  AuditWriteFail = 4,
  HealthProbeTimeout = 5,
  OTAVerifyFail = 6,
  OTARollbackFail = 7,
  OTABootConfirmTimeout = 8,
};

struct InfraErrorMapping {
  InfraErrorCode infra_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

[[nodiscard]] std::string_view infra_error_code_name(InfraErrorCode code);
[[nodiscard]] InfraErrorMapping map_infra_error_code(InfraErrorCode code);

}  // namespace dasall::infra