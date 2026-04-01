#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace dasall::infra::metrics {

struct ExportBatchReport {
  std::uint64_t success_count = 0;
  std::uint64_t fail_count = 0;
  double latency_ms = 0.0;
  std::uint64_t dropped_count = 0;

  [[nodiscard]] bool is_valid() const {
    return std::isfinite(latency_ms) && latency_ms >= 0.0;
  }

  [[nodiscard]] bool has_failures() const {
    return fail_count > 0 || dropped_count > 0;
  }
};

struct MetricsModuleSnapshot {
  std::uint64_t queue_depth = 0;
  std::uint64_t guard_reject_total = 0;
  std::string exporter_state = "uninitialized";
  bool degraded = false;

  [[nodiscard]] bool is_valid() const {
    return !exporter_state.empty();
  }

  [[nodiscard]] bool is_healthy() const {
    return is_valid() && !degraded;
  }
};

}  // namespace dasall::infra::metrics