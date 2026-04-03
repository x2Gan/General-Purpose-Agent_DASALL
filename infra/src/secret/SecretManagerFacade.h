#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "secret/ISecretBackend.h"
#include "secret/ISecretManager.h"

#include "SecretLeaseRegistry.h"

namespace dasall::infra::secret {

struct SecretManagerFacadeOptions {
  std::int64_t handle_ttl_ms = 60000;
  std::string redaction_prefix = "redacted://secret/";
};

class SecretManagerFacade final : public ISecretManager {
 public:
  explicit SecretManagerFacade(std::shared_ptr<ISecretBackend> backend = nullptr,
                               SecretManagerFacadeOptions options = {});

  void set_backend(std::shared_ptr<ISecretBackend> backend);

  [[nodiscard]] std::size_t active_lease_count() const;
  [[nodiscard]] bool has_cached_descriptor(std::string_view secret_name) const;

  [[nodiscard]] SecretHandleResult get_secret(
      const SecretQuery& query,
      const SecretAccessContext& access_context) override;

  [[nodiscard]] SecretMaterializationResult materialize(
      const SecretHandle& handle,
      const SecretAccessContext& access_context) override;

  [[nodiscard]] SecretLifecycleResult release(
      const SecretLease& lease) override;

  [[nodiscard]] RotationResult rotate(
      const RotationRequest& request) override;

  [[nodiscard]] SecretLifecycleResult revoke(
      std::string_view secret_name,
      std::string_view reason_code) override;

  [[nodiscard]] SecretInspectionResult inspect(
      std::string_view secret_name) const override;

 private:
  struct CachedSecretMetadata {
    SecretDescriptor descriptor;
    std::string version;
    std::string backend_ref;
  };

  [[nodiscard]] bool backend_ready() const;

  std::shared_ptr<ISecretBackend> backend_;
  SecretManagerFacadeOptions options_;
  SecretLeaseRegistry lease_registry_;
  std::map<std::string, CachedSecretMetadata> cached_secrets_;
};

}  // namespace dasall::infra::secret