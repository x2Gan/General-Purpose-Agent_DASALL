#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ICapabilityCache.h"
#include "health/HealthStateTypes.h"
#include "health/IHealthProbe.h"
#include "route/ToolRouteSelector.h"

namespace dasall::tools::ops {

inline constexpr std::string_view kToolHealthProbeName = "tools.execution";
inline constexpr std::string_view kToolHealthProbeGroup = "readiness";
inline constexpr std::int64_t kToolHealthProbeIntervalMs = 5000;
inline constexpr std::int64_t kToolHealthProbeTimeoutMs = 100;
inline constexpr std::string_view kToolHealthDetailNamespace =
    "status://tools/health";

struct ToolRegistryHealthSample {
  std::uint64_t revision = 1U;
  bool descriptor_catalog_ready = true;
  bool delta_pipeline_degraded = false;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct ToolLaneHealthSample {
  bool available = true;
  std::uint32_t concurrency_budget = 1U;
  bool saturated = false;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }

  [[nodiscard]] bool blocks_route() const {
    return !available || concurrency_budget == 0U || saturated;
  }
};

struct ToolMCPHealthSample {
  bool session_ready = true;
  CapabilityFreshness freshness = CapabilityFreshness::fresh;
  bool stale_read_allowed = false;
  std::optional<std::string> last_error;
  std::optional<std::string> trust_marker;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }

  [[nodiscard]] bool blocks_route() const {
    if (!session_ready) {
      return true;
    }

    if (freshness == CapabilityFreshness::expired) {
      return true;
    }

    if (freshness == CapabilityFreshness::stale && !stale_read_allowed) {
      return true;
    }

    return false;
  }

  [[nodiscard]] bool is_degraded() const {
    return !session_ready || freshness != CapabilityFreshness::fresh ||
           last_error.has_value();
  }
};

struct ToolHealthSample {
  ToolRegistryHealthSample registry{};
  ToolLaneHealthSample builtin_lane{};
  ToolLaneHealthSample workflow_lane{};
  ToolMCPHealthSample mcp{};
  bool audit_bridge_degraded = false;
  bool metrics_bridge_degraded = false;
  bool trace_bridge_degraded = false;
  std::int64_t latency_ms = 0;
  std::int64_t sampled_at_unix_ms = 0;
  std::string detail_ref = std::string(kToolHealthDetailNamespace) + "/sample";

  [[nodiscard]] bool has_consistent_values() const {
    return latency_ms >= 0 && sampled_at_unix_ms > 0 &&
           registry.has_consistent_values() &&
           builtin_lane.has_consistent_values() &&
           workflow_lane.has_consistent_values() && mcp.has_consistent_values();
  }
};

class IToolHealthSignalProvider {
 public:
  virtual ~IToolHealthSignalProvider() = default;

  [[nodiscard]] virtual ToolHealthSample sample(std::int64_t timeout_ms) = 0;
};

struct ToolHealthProbeOptions {
  std::string detail_namespace = std::string(kToolHealthDetailNamespace);
  std::function<std::int64_t()> now_ms;
};

class ToolHealthProbe final : public infra::IHealthProbe {
 public:
  explicit ToolHealthProbe(
      std::shared_ptr<IToolHealthSignalProvider> signal_provider,
      ToolHealthProbeOptions options = {});

  [[nodiscard]] const infra::ProbeDescriptor& descriptor() const {
    return descriptor_;
  }

  [[nodiscard]] infra::ProbeResult probe() override;
  [[nodiscard]] infra::HealthSnapshot snapshot() const;
  [[nodiscard]] route::ToolRouteHealthSnapshot route_health_snapshot() const;

 private:
  [[nodiscard]] static infra::ProbeDescriptor make_descriptor();
  [[nodiscard]] static infra::HealthSnapshot build_snapshot(
      const ToolHealthSample& sample,
      std::uint64_t version);
  [[nodiscard]] static route::ToolRouteHealthSnapshot build_route_health(
      const ToolHealthSample& sample);
  [[nodiscard]] static infra::ProbeStatus probe_status_for(
      const infra::HealthSnapshot& snapshot);
  [[nodiscard]] std::string detail_ref_for(
      const ToolHealthSample& sample,
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

  static void collect_registry_health(const ToolHealthSample& sample,
                                      infra::HealthSnapshot* snapshot);
  static void collect_lane_health(
      const ToolHealthSample& sample,
      infra::HealthSnapshot* snapshot,
      route::ToolRouteHealthSnapshot* route_health);
  static void collect_mcp_health(const ToolHealthSample& sample,
                                 infra::HealthSnapshot* snapshot,
                                 route::ToolRouteHealthSnapshot* route_health);

  std::shared_ptr<IToolHealthSignalProvider> signal_provider_;
  ToolHealthProbeOptions options_{};
  infra::ProbeDescriptor descriptor_ = make_descriptor();
  std::optional<infra::HealthSnapshot> latest_snapshot_;
  route::ToolRouteHealthSnapshot latest_route_health_{};
  std::uint64_t next_snapshot_version_ = 1U;
};

}  // namespace dasall::tools::ops