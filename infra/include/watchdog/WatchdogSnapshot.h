#pragma once

#include <cstdint>

namespace dasall::infra::watchdog {

struct WatchdogSnapshot {
  std::uint64_t version = 0;
  std::uint32_t total_entities = 0;
  std::uint32_t timed_out_entities = 0;
  std::uint32_t degraded_entities = 0;
  std::int64_t scan_lag_ms = 0;
  std::int64_t ts = 0;

  [[nodiscard]] bool has_consistent_counts() const {
    return version > 0 && total_entities >= timed_out_entities &&
           total_entities >= degraded_entities && scan_lag_ms >= 0 && ts > 0;
  }

  [[nodiscard]] bool is_newer_than(const WatchdogSnapshot& previous) const {
    return version > previous.version && ts >= previous.ts;
  }
};

}  // namespace dasall::infra::watchdog