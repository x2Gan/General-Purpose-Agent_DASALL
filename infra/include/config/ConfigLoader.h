#pragma once

#include <filesystem>
#include <string>

#include "config/IConfigLoader.h"

namespace dasall::infra::config {

struct ConfigLoaderOptions {
  std::filesystem::path repository_root;
  std::string runtime_overlay_source_ref;
};

class ConfigLoader final : public IConfigLoader {
 public:
  explicit ConfigLoader(ConfigLoaderOptions options = {});

  ConfigLoadResult load_default() override;
  ConfigLoadResult load_profile(std::string_view profile_id) override;
  ConfigLoadResult load_deploy(std::string_view source_ref) override;
  ConfigLoadResult load_runtime_overlay() override;

 private:
  [[nodiscard]] std::filesystem::path resolve_repository_root() const;

  ConfigLoaderOptions options_;
};

}  // namespace dasall::infra::config