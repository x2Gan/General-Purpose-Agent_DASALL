#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "config/ConfigCommandTypes.h"
#include "config/DaemonConfigFileStore.h"

namespace dasall::apps::cli::config {

class ConfigDiffPlanner {
 public:
  [[nodiscard]] std::optional<DesiredConfigSnapshot> load_desired_from_file(
      const std::filesystem::path& path,
      std::string* error_message = nullptr) const;

  [[nodiscard]] ConfigActionPlan build_plan(
      const DesiredConfigSnapshot& current,
      const DesiredConfigSnapshot& desired,
      InstallState state_before,
      const DaemonConfigFileStorePaths& store_paths) const;
};

}  // namespace dasall::apps::cli::config