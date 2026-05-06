#pragma once

#include <cstdint>

namespace dasall::infra {

struct HealthResolvedConfig {
  bool enabled = true;
  std::uint32_t liveness_interval_ms = 2000U;
  std::uint32_t readiness_interval_ms = 5000U;
  std::uint32_t probe_timeout_ms = 1000U;
  std::uint32_t degraded_threshold = 1U;
  std::uint32_t unhealthy_consecutive_failures = 3U;
  std::uint32_t history_window_size = 20U;
  bool event_on_transition_only = true;
  bool recovery_hint_enabled = true;

  [[nodiscard]] bool is_valid() const {
    if (liveness_interval_ms == 0U || readiness_interval_ms == 0U ||
        probe_timeout_ms == 0U || degraded_threshold == 0U ||
        unhealthy_consecutive_failures == 0U || history_window_size == 0U) {
      return false;
    }

    if (probe_timeout_ms > liveness_interval_ms ||
        probe_timeout_ms > readiness_interval_ms) {
      return false;
    }

    return degraded_threshold <= unhealthy_consecutive_failures;
  }
};

}  // namespace dasall::infra