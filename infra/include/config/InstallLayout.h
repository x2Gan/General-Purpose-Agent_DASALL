#pragma once

#include <filesystem>

namespace dasall::infra::config {

struct InstallLayout {
  std::filesystem::path readonly_assets_root;
  std::filesystem::path profiles_root;
  std::filesystem::path llm_prompts_root;
  std::filesystem::path llm_providers_root;
  std::filesystem::path daemon_config_path;
  std::filesystem::path daemon_socket_path;
  std::filesystem::path state_root;

  [[nodiscard]] bool has_consistent_values() const;
};

[[nodiscard]] InstallLayout packaged_install_layout();
[[nodiscard]] InstallLayout resolve_install_layout();

}  // namespace dasall::infra::config