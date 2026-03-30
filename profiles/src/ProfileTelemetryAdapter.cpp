#include "ProfileTelemetryAdapter.h"

#include <array>
#include <string>

#include "error/ResultCode.h"

namespace dasall::profiles {
namespace {

[[nodiscard]] ProfileTelemetryDispatchResult invalid_input_result(std::string_view stage,
                                                                  std::string_view detail) {
  return ProfileTelemetryDispatchResult{
      .log_result = infra::LogWriteResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string(detail),
          std::string(stage),
          "ProfileTelemetryAdapter"),
      .audit_result = infra::audit::AuditWriteResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string(detail),
          std::string(stage),
          "ProfileTelemetryAdapter"),
  };
}

[[nodiscard]] bool all_fields_present(const std::array<std::string_view, 6>& required_fields) {
  for (const std::string_view field : required_fields) {
    if (field.empty()) {
      return false;
    }
  }

  return true;
}

}  // namespace

ProfileTelemetryAdapter::ProfileTelemetryAdapter(infra::ILogger& logger,
                                                 infra::audit::IAuditLogger& audit_logger)
    : logger_(logger), audit_logger_(audit_logger) {}

ProfileTelemetryDispatchResult ProfileTelemetryAdapter::record_activation_success(
    std::string_view requested_profile_id,
    std::string_view effective_profile_id,
    std::string_view activation_mode,
    std::string_view actor) {
  return emit_event("runtime_policy_activated",
                    requested_profile_id,
                    effective_profile_id,
                    activation_mode,
                    "activation_success",
                    actor,
                    infra::LogLevel::Info,
                    infra::AuditOutcome::Succeeded);
}

ProfileTelemetryDispatchResult ProfileTelemetryAdapter::record_reload_rejected(
    std::string_view profile_id,
    std::string_view reason_code,
    std::string_view actor) {
  return emit_event("runtime_policy_reload_rejected",
                    profile_id,
                    profile_id,
                    "reload_rejected",
                    reason_code,
                    actor,
                    infra::LogLevel::Warn,
                    infra::AuditOutcome::Rejected);
}

ProfileTelemetryDispatchResult ProfileTelemetryAdapter::record_fallback_lkg(
    std::string_view requested_profile_id,
    std::string_view effective_profile_id,
    std::string_view reason_code,
    std::string_view actor) {
  return emit_event("runtime_policy_fallback_lkg",
                    requested_profile_id,
                    effective_profile_id,
                    "fallback_lkg",
                    reason_code,
                    actor,
                    infra::LogLevel::Error,
                    infra::AuditOutcome::Escalated);
}

ProfileTelemetryDispatchResult ProfileTelemetryAdapter::emit_event(
    std::string_view action,
    std::string_view requested_profile_id,
    std::string_view effective_profile_id,
    std::string_view activation_mode,
    std::string_view reason_code,
    std::string_view actor,
    infra::LogLevel level,
    infra::AuditOutcome outcome) {
  if (!all_fields_present(
          {action, requested_profile_id, effective_profile_id, activation_mode, reason_code, actor})) {
    return invalid_input_result("profiles.telemetry.emit",
                                "profile telemetry fields must be specified");
  }

  const infra::LogEvent log_event{
      .level = level,
      .module = "profiles.telemetry",
      .message = std::string(action),
      .attrs = {{"requested_profile_id", std::string(requested_profile_id)},
                {"effective_profile_id", std::string(effective_profile_id)},
                {"activation_mode", std::string(activation_mode)},
                {"reason_code", std::string(reason_code)},
                {"actor", std::string(actor)}},
      .ts = 0,
  };

  const infra::AuditEvent audit_event{
      .event_id = std::string("profile-audit-") + std::string(action) + "-" +
            std::string(effective_profile_id),
      .action = std::string(action),
      .actor = std::string(actor),
      .target = "profile:" + std::string(effective_profile_id),
      .outcome = outcome,
      .evidence_ref = {.kind = infra::AuditEvidenceKind::RecoveryOutcome,
                       .ref = std::string(action) + ":" + std::string(reason_code)},
      .side_effects = {"activation_mode:" + std::string(activation_mode),
                       "reason_code:" + std::string(reason_code),
                       "requested_profile_id:" + std::string(requested_profile_id)},
      .timestamp = 1,
  };

  return ProfileTelemetryDispatchResult{
      .log_result = logger_.log(log_event),
      .audit_result = audit_logger_.write_audit(audit_event),
  };
}

}  // namespace dasall::profiles