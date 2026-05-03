#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DaemonConfig.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::apps::daemon {

inline constexpr char kDefaultDaemonEntryProfileId[] = "desktop_full";

struct DaemonEntryConfigLoadRequest {
  std::filesystem::path profiles_root = "profiles";
  std::string requested_profile_id = kDefaultDaemonEntryProfileId;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::string> socket_path_override;

  [[nodiscard]] bool has_consistent_values() const {
    return !profiles_root.empty() && !requested_profile_id.empty() &&
           (!deployment_config_path.has_value() || !deployment_config_path->empty()) &&
           (!socket_path_override.has_value() || !socket_path_override->empty());
  }
};

struct DaemonEntryConfig {
  DaemonBootstrapConfig bootstrap_config;
  std::string requested_profile_id;
  std::string effective_profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> runtime_policy_snapshot;
  std::optional<std::string> config_revision;
  std::vector<DaemonConfigConflict> conflicts;

  [[nodiscard]] bool has_consistent_values() const {
    return bootstrap_config.has_consistent_values() && !requested_profile_id.empty() &&
           !effective_profile_id.empty() && runtime_policy_snapshot != nullptr &&
           runtime_policy_snapshot->has_consistent_values();
  }
};

struct DaemonEntryConfigLoadResult {
  std::optional<DaemonEntryConfig> entry_config;
  std::string message;

  [[nodiscard]] bool ok() const {
    return entry_config.has_value() && entry_config->has_consistent_values();
  }
};

class DaemonEntryConfigLoader {
 public:
  [[nodiscard]] DaemonEntryConfigLoadResult load(
      const DaemonEntryConfigLoadRequest& request) const;
};

}  // namespace dasall::apps::daemon