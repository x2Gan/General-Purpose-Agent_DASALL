#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "health/HealthStateTypes.h"
#include "health/IHealthProbe.h"

namespace dasall::runtime {

inline constexpr std::string_view kRuntimeHealthProbeName = "runtime.control_plane";
inline constexpr std::string_view kRuntimeHealthProbeGroup = "readiness";
inline constexpr std::int64_t kRuntimeHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kRuntimeHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kRuntimeHealthDetailNamespace = "status://runtime/health";

struct RuntimeHealthSample {
  bool dependencies_ready = true;
  bool watchdog_healthy = true;
  bool telemetry_degraded = false;
  bool event_bus_overflow = false;
  bool maintenance_backlog = false;
  bool safe_mode_active = false;
  std::vector<std::string> failed_components;
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref = std::string(kRuntimeHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const;
};

class IRuntimeHealthSignalProvider {
 public:
  virtual ~IRuntimeHealthSignalProvider() = default;

  [[nodiscard]] virtual RuntimeHealthSample sample(std::int64_t timeout_ms) = 0;
};

struct RuntimeHealthProbeOptions {
  std::string detail_namespace = std::string(kRuntimeHealthDetailNamespace);
  std::function<std::int64_t()> now_ms;
};

class RuntimeHealthProbe final : public infra::IHealthProbe {
 public:
  explicit RuntimeHealthProbe(
      std::shared_ptr<IRuntimeHealthSignalProvider> signal_provider,
      RuntimeHealthProbeOptions options = {});

  [[nodiscard]] const infra::ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] infra::HealthSnapshot collect_snapshot();
  [[nodiscard]] infra::ProbeResult probe() override;
  [[nodiscard]] infra::HealthSnapshot snapshot() const;

 private:
  [[nodiscard]] static infra::ProbeDescriptor make_descriptor();
  [[nodiscard]] static infra::HealthSnapshot build_snapshot(
      const RuntimeHealthSample& sample,
      std::uint64_t version);
  [[nodiscard]] static infra::ProbeStatus probe_status_for(
      const infra::HealthSnapshot& snapshot);
  [[nodiscard]] RuntimeHealthSample fallback_sample(std::string component) const;
  [[nodiscard]] std::string detail_ref_for(
      const RuntimeHealthSample& sample,
      const infra::HealthSnapshot& snapshot) const;
  [[nodiscard]] std::int64_t current_time_unix_ms() const;
  [[nodiscard]] std::string make_detail_ref(std::string_view suffix) const;

  std::shared_ptr<IRuntimeHealthSignalProvider> signal_provider_;
  RuntimeHealthProbeOptions options_{};
  infra::ProbeDescriptor descriptor_ = make_descriptor();
  mutable std::mutex snapshot_mutex_;
  std::optional<RuntimeHealthSample> latest_sample_;
  std::optional<infra::HealthSnapshot> latest_snapshot_;
  std::uint64_t next_snapshot_version_ = 1U;
};

}  // namespace dasall::runtime