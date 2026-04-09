#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ServiceTypes.h"

namespace dasall::services::internal {

struct InternalSnapshotQuery {
  bool include_service_registry = true;
  bool include_platform_snapshot = true;
  bool strict_health = false;
};

struct InternalSystemSnapshot {
  std::string snapshot_json;
  std::uint64_t captured_at_ms = 0U;
  std::vector<std::string> service_instances;
  bool infra_health_ok = false;
  bool degraded = false;
  std::string error_message;
};

struct SystemSnapshotLaneDependencies {
  std::function<std::string()> load_infra_health_snapshot;
  std::function<std::string()> load_platform_snapshot;
  std::function<std::string()> load_resource_summary;
  std::function<std::vector<std::string>()> load_service_instances;
  std::function<std::uint64_t()> now_ms;
};

class SystemSnapshotLane {
 public:
  explicit SystemSnapshotLane(SystemSnapshotLaneDependencies dependencies);

  [[nodiscard]] InternalSystemSnapshot query_snapshot(
      const ServiceCallContext& context,
      const InternalSnapshotQuery& request) const;

 private:
  [[nodiscard]] InternalSystemSnapshot make_snapshot_error(const std::string& message,
                                                           std::uint64_t captured_at_ms) const;
  [[nodiscard]] std::uint64_t current_time_ms() const;

  SystemSnapshotLaneDependencies dependencies_;
};

}  // namespace dasall::services::internal