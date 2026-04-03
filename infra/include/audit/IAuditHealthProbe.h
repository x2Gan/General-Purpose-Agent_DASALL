#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class AuditHealthState {
  Ready = 0,
  Degraded = 1,
  Unavailable = 2,
};

inline constexpr std::string_view kAuditHealthDetailNamespace =
    "diag://infra/audit/health";

inline constexpr std::array<std::string_view, 3>
    kAuditHealthDegradedFailureReasons = {
        "primary_capacity_exhausted",
        "fallback_active",
        "metrics_bridge_degraded",
};

inline constexpr std::array<std::string_view, 3>
    kAuditHealthUnavailableFailureReasons = {
        "fallback_unavailable",
        "service_not_started",
        "service_stopped",
};

[[nodiscard]] inline bool is_audit_health_failure_reason(std::string_view reason) {
  return std::find(kAuditHealthDegradedFailureReasons.begin(),
                   kAuditHealthDegradedFailureReasons.end(),
                   reason) != kAuditHealthDegradedFailureReasons.end() ||
         std::find(kAuditHealthUnavailableFailureReasons.begin(),
                   kAuditHealthUnavailableFailureReasons.end(),
                   reason) != kAuditHealthUnavailableFailureReasons.end();
}

[[nodiscard]] inline bool is_audit_health_reason_for_state(
    AuditHealthState state,
    std::string_view reason) {
  if (state == AuditHealthState::Ready) {
    return reason.empty();
  }

  if (state == AuditHealthState::Degraded) {
    return std::find(kAuditHealthDegradedFailureReasons.begin(),
                     kAuditHealthDegradedFailureReasons.end(),
                     reason) != kAuditHealthDegradedFailureReasons.end();
  }

  return std::find(kAuditHealthUnavailableFailureReasons.begin(),
                   kAuditHealthUnavailableFailureReasons.end(),
                   reason) != kAuditHealthUnavailableFailureReasons.end();
}

struct AuditHealthStatus {
  AuditHealthState state = AuditHealthState::Unavailable;
  std::string last_failure_reason;
  std::string detail_ref;
  std::optional<contracts::ResultCode> error_code;
  std::int64_t sampled_at_unix_ms = 0;
  bool fallback_active = false;
  bool metrics_bridge_degraded = false;

  [[nodiscard]] bool has_valid_error_mapping() const {
    return !error_code.has_value() ||
           contracts::classify_result_code(*error_code) !=
               contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_consistent_state() const {
    if (sampled_at_unix_ms <= 0 || !has_valid_error_mapping()) {
      return false;
    }

    if (!last_failure_reason.empty() &&
        !is_audit_health_failure_reason(last_failure_reason)) {
      return false;
    }

    if (state == AuditHealthState::Ready) {
      return last_failure_reason.empty() && !error_code.has_value() &&
             !fallback_active && !metrics_bridge_degraded;
    }

    if (last_failure_reason.empty() || detail_ref.empty() ||
        !is_audit_health_reason_for_state(state, last_failure_reason)) {
      return false;
    }

    if (last_failure_reason == "fallback_active" && !fallback_active) {
      return false;
    }

    if (last_failure_reason == "metrics_bridge_degraded" &&
        !metrics_bridge_degraded) {
      return false;
    }

    if (state == AuditHealthState::Degraded) {
      return true;
    }

    return error_code.has_value() && !fallback_active;
  }
};

}  // namespace dasall::infra

namespace dasall::infra::audit {

class IAuditHealthProbe {
 public:
  virtual ~IAuditHealthProbe() = default;

  [[nodiscard]] virtual AuditHealthStatus evaluate() const = 0;
};

}  // namespace dasall::infra::audit
