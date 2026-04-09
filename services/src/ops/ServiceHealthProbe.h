#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "adapters/AdapterRouter.h"
#include "health/HealthStateTypes.h"
#include "health/IHealthProbe.h"

namespace dasall::services::internal {

inline constexpr std::string_view kServiceHealthProbeName =
    "services.capability";
inline constexpr std::string_view kServiceHealthProbeGroup = "readiness";
inline constexpr std::int64_t kServiceHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kServiceHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kServiceHealthDetailNamespace =
    "status://services/health";

enum class ServiceCircuitState {
  closed = 0,
  open,
  half_open,
  unknown,
};

struct ServiceQueueSnapshot {
  std::uint32_t depth = 0U;
  std::uint32_t high_watermark = 1U;
  std::uint64_t overflow_total = 0U;
  bool resync_required = false;

  [[nodiscard]] bool has_consistent_values() const {
    return high_watermark > 0U;
  }

  [[nodiscard]] bool blocks_readiness() const {
    return depth >= high_watermark || overflow_total > 0U || resync_required;
  }
};

struct ServiceHealthSample {
  ServiceCircuitState circuit_state = ServiceCircuitState::closed;
  AdapterAvailabilityState adapter_readiness = AdapterAvailabilityState::available;
  ServiceQueueSnapshot command_queue{};
  ServiceQueueSnapshot subscription_queue{};
  bool system_snapshot_ready = true;
  bool system_snapshot_degraded = false;
  bool audit_bridge_degraded = false;
  bool metrics_bridge_degraded = false;
  bool trace_bridge_degraded = false;
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref =
      std::string(kServiceHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const {
    return latency_ms >= 0 && sampled_at_unix_ms > 0 &&
           command_queue.has_consistent_values() &&
           subscription_queue.has_consistent_values();
  }
};

class IServiceHealthSignalProvider {
 public:
  virtual ~IServiceHealthSignalProvider() = default;

  [[nodiscard]] virtual ServiceHealthSample sample(std::int64_t timeout_ms) = 0;
};

struct ServiceHealthProbeOptions {
  std::string detail_namespace = std::string(kServiceHealthDetailNamespace);
  std::function<std::int64_t()> now_ms;
};

class ServiceHealthProbe final : public infra::IHealthProbe {
 public:
  explicit ServiceHealthProbe(
      std::shared_ptr<IServiceHealthSignalProvider> signal_provider,
      ServiceHealthProbeOptions options = {});

  [[nodiscard]] const infra::ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] infra::ProbeResult probe() override;
  [[nodiscard]] infra::HealthSnapshot snapshot() const;

 private:
  [[nodiscard]] static infra::ProbeDescriptor make_descriptor();
  [[nodiscard]] static infra::HealthSnapshot build_snapshot(
      const ServiceHealthSample& sample,
      std::uint64_t version);
  [[nodiscard]] static infra::ProbeStatus probe_status_for(
      const infra::HealthSnapshot& snapshot);
  [[nodiscard]] std::string detail_ref_for(
      const ServiceHealthSample& sample,
      const infra::HealthSnapshot& snapshot) const;
  [[nodiscard]] infra::ProbeResult make_failure_result(
      contracts::ResultCode error_code,
      infra::ProbeStatus status,
      std::int64_t latency_ms,
      std::int64_t timestamp_ms,
      std::string detail_ref,
      std::vector<std::string> failed_components);
  [[nodiscard]] std::int64_t current_time_unix_ms() const;
  [[nodiscard]] std::string make_detail_ref(std::string_view suffix) const;

  std::shared_ptr<IServiceHealthSignalProvider> signal_provider_;
  ServiceHealthProbeOptions options_{};
  infra::ProbeDescriptor descriptor_ = make_descriptor();
  std::optional<infra::HealthSnapshot> latest_snapshot_;
  std::uint64_t next_snapshot_version_ = 1U;
};

}  // namespace dasall::services::internal