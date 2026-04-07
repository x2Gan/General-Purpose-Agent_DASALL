#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "health/IHealthProbe.h"
#include "ota/OTATypes.h"

namespace dasall::infra::ota {

inline constexpr std::string_view kOTAHealthProbeName = "infra.ota.status";
inline constexpr std::string_view kOTAHealthProbeGroup = "readiness";
inline constexpr std::int64_t kOTAHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kOTAHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kOTAHealthDetailNamespace =
    "status://ota/health";

struct OTAHealthSignals {
  OTAStatusSnapshot status_snapshot;
  bool audit_bridge_degraded = false;
  bool rollback_degraded = false;
  std::string last_detail_ref;

  [[nodiscard]] std::uint32_t pending_confirm_count() const {
    return status_snapshot.pending_confirm ? 1U : 0U;
  }

  [[nodiscard]] bool has_recent_failure() const {
    return rollback_degraded || status_snapshot.last_failure_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return status_snapshot.is_valid();
  }
};

enum class OTAHealthSampleState {
  Ready = 0,
  Timeout,
  Invalid,
};

struct OTAHealthSample {
  OTAHealthSampleState state = OTAHealthSampleState::Ready;
  OTAHealthSignals signals{};
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref = std::string(kOTAHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const {
    if (latency_ms < 0 || sampled_at_unix_ms <= 0) {
      return false;
    }

    if (state == OTAHealthSampleState::Ready) {
      return signals.has_consistent_values();
    }

    return !detail_ref.empty();
  }
};

class IOTAHealthSignalProvider {
 public:
  virtual ~IOTAHealthSignalProvider() = default;

  [[nodiscard]] virtual OTAHealthSample sample(std::int64_t timeout_ms) = 0;
};

class OTAHealthProbe final : public IHealthProbe {
 public:
  explicit OTAHealthProbe(std::shared_ptr<IOTAHealthSignalProvider> signal_provider);

  [[nodiscard]] const ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] ProbeResult probe() override;

 private:
  [[nodiscard]] static ProbeDescriptor make_descriptor();
  [[nodiscard]] static ProbeStatus map_status(const OTAHealthSignals& signals);
  [[nodiscard]] static std::string detail_ref_for_state(const OTAHealthSignals& signals,
                                                        ProbeStatus status);
  [[nodiscard]] ProbeResult make_failure_result(contracts::ResultCode error_code,
                                                ProbeStatus status,
                                                std::int64_t latency_ms,
                                                std::int64_t timestamp_ms,
                                                std::string detail_ref) const;

  std::shared_ptr<IOTAHealthSignalProvider> signal_provider_;
  ProbeDescriptor descriptor_ = make_descriptor();
};

}  // namespace dasall::infra::ota