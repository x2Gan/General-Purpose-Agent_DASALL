#include "secret/SecretManagerLiveComposition.h"

#include <memory>
#include <string>

#include "config/InstallLayout.h"
#include "secret/SecretManagerFacade.h"
#include "secret/backends/FileSecretBackend.h"

namespace dasall::infra::secret {
namespace {

[[nodiscard]] std::filesystem::path selected_root(
    const std::filesystem::path& default_root,
    const std::filesystem::path& override_root) {
  return override_root.empty() ? default_root : override_root;
}

[[nodiscard]] SecretManagerLiveCompositionResult make_error(std::string error) {
  return SecretManagerLiveCompositionResult{
      .secret_manager = nullptr,
      .error = std::move(error),
  };
}

}  // namespace

SecretManagerLiveCompositionResult compose_live_secret_manager(
    std::string_view secret_backend_type,
    const SecretManagerLiveCompositionOptions& options) {
  if (secret_backend_type != "file") {
    return make_error(std::string("unsupported live secret backend type: ") +
                      std::string(secret_backend_type));
  }

  const auto install_layout = config::resolve_install_layout();
  const auto state_root = selected_root(install_layout.state_root,
                                        options.state_root_override);
  auto secret_backend = std::make_shared<FileSecretBackend>(
      FileSecretBackendOptions{
          .root_dir = state_root / "secrets",
          .encrypt_at_rest = true,
          .backend_ref = "file.primary",
          .lease_duration_ms = 60000,
          .rotation_epoch = 1,
      });

  return SecretManagerLiveCompositionResult{
      .secret_manager =
          std::make_shared<SecretManagerFacade>(std::move(secret_backend)),
      .error = {},
  };
}

}  // namespace dasall::infra::secret