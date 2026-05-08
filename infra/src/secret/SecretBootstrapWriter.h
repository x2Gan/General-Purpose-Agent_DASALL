#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "secret/SecureBuffer.h"

namespace dasall::infra::secret {

enum class SecretProvisioningState {
  Missing = 0,
  Configured = 1,
  RuntimeVerified = 2,
};

[[nodiscard]] std::string_view to_string(SecretProvisioningState state);

struct SecretBootstrapWriterOptions {
  std::filesystem::path root_dir = "/var/lib/dasall/secrets";
  bool encrypt_at_rest = true;
  std::string backend_ref = "file.primary";
  std::string rotation_policy_ref = "rotation/manual";
  std::string owner_ref = "llm-config";
  std::string initial_version = "v1";
};

struct SecretBootstrapRequest {
  std::string provider_ref;
  SecureBuffer secret;
  std::optional<std::string> auth_profile_name;

  [[nodiscard]] bool is_valid() const;
};

struct SecretProvisioningResult {
  bool ok = false;
  std::string auth_ref;
  std::filesystem::path backend_root;
  SecretProvisioningState provisioning_state = SecretProvisioningState::Missing;
  std::string secret_name;
  std::string version;
  std::string error_message;

  [[nodiscard]] static SecretProvisioningResult success(
      std::string auth_ref,
      std::filesystem::path backend_root,
      std::string secret_name,
      std::string version);

  [[nodiscard]] static SecretProvisioningResult failure(
      std::filesystem::path backend_root,
      std::string error_message);

  [[nodiscard]] bool is_valid() const;
};

class SecretBootstrapWriter {
 public:
  explicit SecretBootstrapWriter(
      SecretBootstrapWriterOptions options = SecretBootstrapWriterOptions{});

  [[nodiscard]] SecretProvisioningResult import_secret(
      SecretBootstrapRequest request) const;

 private:
  SecretBootstrapWriterOptions options_;
};

}  // namespace dasall::infra::secret