#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "secret/ISecretManager.h"

namespace dasall::infra::secret {

struct SecretManagerLiveCompositionOptions {
  std::filesystem::path state_root_override;
};

struct SecretManagerLiveCompositionResult {
  std::shared_ptr<ISecretManager> secret_manager;
  std::string error;

  [[nodiscard]] bool ok() const {
    return secret_manager != nullptr && error.empty();
  }
};

[[nodiscard]] SecretManagerLiveCompositionResult compose_live_secret_manager(
    std::string_view secret_backend_type,
    const SecretManagerLiveCompositionOptions& options = {});

}  // namespace dasall::infra::secret