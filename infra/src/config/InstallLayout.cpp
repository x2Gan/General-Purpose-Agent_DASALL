#include "config/InstallLayout.h"

#include <cstdlib>
#include <optional>
#include <system_error>

namespace dasall::infra::config {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool path_is_directory(const fs::path& path) {
  std::error_code error;
  return fs::exists(path, error) && fs::is_directory(path, error);
}

[[nodiscard]] std::optional<fs::path> absolute_path_from_env(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }

  const fs::path path(value);
  if (!path.is_absolute()) {
    return std::nullopt;
  }

  return path;
}

[[nodiscard]] InstallLayout apply_env_overrides(InstallLayout layout) {
  if (const auto state_root = absolute_path_from_env("DASALL_STATE_ROOT");
      state_root.has_value()) {
    layout.state_root = *state_root;
  }

  return layout;
}

[[nodiscard]] std::optional<InstallLayout> source_tree_install_layout() {
#ifdef DASALL_SOURCE_ROOT
  const fs::path source_root = fs::weakly_canonical(fs::path(DASALL_SOURCE_ROOT));
  const fs::path profiles_root = source_root / "profiles";
  const fs::path prompts_root = source_root / "llm" / "assets" / "prompts";
  const fs::path providers_root = source_root / "llm" / "assets" / "providers";
    if (!path_is_directory(profiles_root) || !path_is_directory(prompts_root) ||
      !path_is_directory(providers_root)) {
    return std::nullopt;
  }

  InstallLayout layout = packaged_install_layout();
  layout.readonly_assets_root = source_root;
  layout.profiles_root = profiles_root;
  layout.llm_prompts_root = prompts_root;
  layout.llm_providers_root = providers_root;
  return layout;
#else
  return std::nullopt;
#endif
}

[[nodiscard]] bool packaged_asset_roots_available(const InstallLayout& layout) {
  return path_is_directory(layout.profiles_root) &&
         path_is_directory(layout.llm_prompts_root) &&
         path_is_directory(layout.llm_providers_root);
}

}  // namespace

bool InstallLayout::has_consistent_values() const {
  return readonly_assets_root.is_absolute() && profiles_root.is_absolute() &&
         llm_prompts_root.is_absolute() && llm_providers_root.is_absolute() &&
         daemon_config_path.is_absolute() && daemon_socket_path.is_absolute() &&
         state_root.is_absolute();
}

InstallLayout packaged_install_layout() {
  return apply_env_overrides(InstallLayout{
      .readonly_assets_root = "/usr/share/dasall",
      .profiles_root = "/usr/share/dasall/profiles",
      .llm_prompts_root = "/usr/share/dasall/llm/prompts",
      .llm_providers_root = "/usr/share/dasall/llm/providers",
      .daemon_config_path = "/etc/dasall/daemon.json",
      .daemon_socket_path = "/run/dasall/daemon.sock",
      .state_root = "/var/lib/dasall",
  });
}

InstallLayout resolve_install_layout() {
  const InstallLayout packaged = packaged_install_layout();
  if (packaged_asset_roots_available(packaged)) {
    return packaged;
  }

  if (const auto source_layout = source_tree_install_layout();
      source_layout.has_value()) {
    return *source_layout;
  }

  return packaged;
}

}  // namespace dasall::infra::config