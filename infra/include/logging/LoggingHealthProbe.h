#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "health/IHealthProbe.h"

namespace dasall::infra::logging {

inline constexpr std::string_view kLoggingHealthProbeName =
    "infra.logging.pipeline";
inline constexpr std::string_view kLoggingHealthProbeGroup = "readiness";
inline constexpr std::int64_t kLoggingHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kLoggingHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kLoggingHealthDetailNamespace =
    "diag://infra/logging/health";

struct LoggingHealthSignals {
  std::uint32_t queue_depth = 0;
  std::uint32_t queue_high_watermark = 1;
  std::uint64_t dropped_total_delta = 0;
  bool recovery_degraded = false;
  bool fallback_active = false;
  std::uint64_t unrecoverable_failure_total = 0;
  bool metrics_bridge_degraded = false;

  [[nodiscard]] bool has_consistent_values() const {
    return queue_high_watermark > 0;
  }

  [[nodiscard]] bool queue_above_high_watermark() const {
    return queue_depth >= queue_high_watermark;
  }
};

enum class LoggingHealthSampleState {
  Ready = 0,
  Timeout,
  Invalid,
};

struct LoggingHealthSample {
  LoggingHealthSampleState state = LoggingHealthSampleState::Ready;
  LoggingHealthSignals signals{};
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref = std::string(kLoggingHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const {
    if (latency_ms < 0 || sampled_at_unix_ms <= 0) {
      return false;
    }

    if (state == LoggingHealthSampleState::Ready) {
      return signals.has_consistent_values();
    }

    return !detail_ref.empty();
  }
};

class ILoggingHealthSignalProvider {
 public:
  virtual ~ILoggingHealthSignalProvider() = default;

  [[nodiscard]] virtual LoggingHealthSample sample(std::int64_t timeout_ms) = 0;
};

class LoggingHealthProbe final : public IHealthProbe {
 public:
  explicit LoggingHealthProbe(
      std::shared_ptr<ILoggingHealthSignalProvider> signal_provider);

  [[nodiscard]] const ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] ProbeResult probe() override;

 private:
  [[nodiscard]] static ProbeDescriptor make_descriptor();
  [[nodiscard]] static ProbeStatus map_status(
      const LoggingHealthSignals& signals);
  [[nodiscard]] static std::string detail_ref_for_state(
      const LoggingHealthSignals& signals,
      ProbeStatus status);
  [[nodiscard]] ProbeResult make_failure_result(
      contracts::ResultCode error_code,
      ProbeStatus status,
      std::int64_t latency_ms,
      std::int64_t timestamp_ms,
      std::string detail_ref) const;

  std::shared_ptr<ILoggingHealthSignalProvider> signal_provider_;
  ProbeDescriptor descriptor_ = make_descriptor();
};

}  // namespace dasall::infra::logging