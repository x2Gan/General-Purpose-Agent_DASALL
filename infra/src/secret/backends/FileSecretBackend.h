#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "secret/ISecretBackend.h"

namespace dasall::infra::secret {

struct FileSecretBackendOptions {
  std::filesystem::path root_dir = "secrets";
  bool encrypt_at_rest = true;
  std::string backend_ref = "file.primary";
  std::int64_t lease_duration_ms = 60000;
  std::uint64_t rotation_epoch = 1;
};

class FileSecretBackend final : public ISecretBackend {
 public:
  explicit FileSecretBackend(FileSecretBackendOptions options = {});

  [[nodiscard]] SecretBackendFetchResult fetch_record(
      const SecretQuery& query) override;

  [[nodiscard]] SecretMaterializationResult materialize_record(
      const SecretBackendRecord& record,
      const SecretAccessContext& access_context) override;

  [[nodiscard]] RotationResult promote_version(
      const SecretVersionPromotionRequest& request) override;

  [[nodiscard]] SecretLifecycleResult revoke_version(
      std::string_view secret_name,
      std::string_view version) override;

  [[nodiscard]] SecretBackendStatus get_backend_status() const override;

 private:
  [[nodiscard]] std::optional<std::filesystem::path> resolve_secret_path(
      std::string_view secret_name) const;
  [[nodiscard]] bool root_available() const;

  FileSecretBackendOptions options_;
  std::optional<contracts::ResultCode> last_error_code_;
};

}  // namespace dasall::infra::secret