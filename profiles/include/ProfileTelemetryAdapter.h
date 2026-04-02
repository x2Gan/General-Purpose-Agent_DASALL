#pragma once

#include <string_view>

#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"

namespace dasall::profiles {

struct ProfileTelemetryDispatchResult {
  infra::logging::LogWriteResult log_result;
  infra::AuditWriteOutcome audit_result;

  [[nodiscard]] bool ok() const {
    return log_result.ok &&
           (audit_result.is_success() || audit_result.is_degraded_success());
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return log_result.references_only_contract_error_types() &&
           (!audit_result.error_code.has_value() ||
            contracts::classify_result_code(*audit_result.error_code) !=
                contracts::ResultCodeCategory::Unknown);
  }
};

class ProfileTelemetryAdapter {
 public:
  ProfileTelemetryAdapter(infra::logging::ILogger& logger,
                          infra::audit::IAuditLogger& audit_logger);

  [[nodiscard]] ProfileTelemetryDispatchResult record_activation_success(
      std::string_view requested_profile_id,
      std::string_view effective_profile_id,
      std::string_view activation_mode,
      std::string_view actor = "profiles.telemetry");

  [[nodiscard]] ProfileTelemetryDispatchResult record_reload_rejected(
      std::string_view profile_id,
      std::string_view reason_code,
      std::string_view actor = "profiles.telemetry");

  [[nodiscard]] ProfileTelemetryDispatchResult record_fallback_lkg(
      std::string_view requested_profile_id,
      std::string_view effective_profile_id,
      std::string_view reason_code,
      std::string_view actor = "profiles.telemetry");

 private:
  [[nodiscard]] ProfileTelemetryDispatchResult emit_event(
      std::string_view action,
      std::string_view requested_profile_id,
      std::string_view effective_profile_id,
      std::string_view activation_mode,
      std::string_view reason_code,
      std::string_view actor,
      infra::LogLevel level,
      infra::AuditOutcome outcome);

  infra::logging::ILogger& logger_;
  infra::audit::IAuditLogger& audit_logger_;
};

}  // namespace dasall::profiles