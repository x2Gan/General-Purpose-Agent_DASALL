#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class ProbeCriticality {
  Unspecified = 0,
  Critical = 1,
  NonCritical = 2,
};

enum class ProbeStatus {
  Unknown = 0,
  Healthy = 1,
  Degraded = 2,
  Unhealthy = 3,
};

inline constexpr std::array<std::string_view, 2> kSupportedHealthProbeGroups = {
    "liveness",
    "readiness",
};

[[nodiscard]] inline bool is_supported_health_probe_group(std::string_view group) {
  return std::find(kSupportedHealthProbeGroups.begin(),
                   kSupportedHealthProbeGroups.end(),
                   group) != kSupportedHealthProbeGroups.end();
}

struct ProbeDescriptor {
  std::string probe_name;
  std::string group;
  ProbeCriticality criticality = ProbeCriticality::Unspecified;
  std::int64_t interval_ms = 0;
  std::int64_t timeout_ms = 0;

  [[nodiscard]] bool has_required_fields() const {
    return !probe_name.empty() && is_supported_health_probe_group(group) &&
           criticality != ProbeCriticality::Unspecified && interval_ms > 0 &&
           timeout_ms > 0 && timeout_ms <= interval_ms;
  }
};

struct ProbeResult {
  std::string probe_name;
  ProbeStatus status = ProbeStatus::Unknown;
  std::int64_t latency_ms = 0;
  std::optional<contracts::ResultCode> error_code;
  std::string detail_ref;
  std::int64_t timestamp = 0;

  [[nodiscard]] bool has_required_fields() const {
    return !probe_name.empty() && latency_ms >= 0 && timestamp > 0;
  }

  [[nodiscard]] bool has_valid_error_mapping() const {
    return !error_code.has_value() ||
           contracts::classify_result_code(*error_code) !=
               contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_observable_failure_detail() const {
    if (status == ProbeStatus::Healthy && !error_code.has_value()) {
      return true;
    }

    if (status == ProbeStatus::Degraded || status == ProbeStatus::Unhealthy ||
        error_code.has_value()) {
      return !detail_ref.empty();
    }

    return true;
  }

  [[nodiscard]] bool has_consistent_state() const {
    if (!has_required_fields() || !has_valid_error_mapping() ||
        !has_observable_failure_detail()) {
      return false;
    }

    if (status == ProbeStatus::Healthy && error_code.has_value()) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::infra