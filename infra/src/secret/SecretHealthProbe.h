#pragma once

#include <cstdint>
#include <memory>

#include "secret/ISecretBackend.h"
#include "secret/ISecretHealthSource.h"

#include "SecretRotationCoordinator.h"

namespace dasall::infra::secret {

struct SecretHealthProbeSignals {
  SecretBackendStatus backend_status;
  bool cache_stale = false;
  std::uint64_t active_lease_count = 0;
  SecretRotationCoordinatorStatus rotation_status;

  [[nodiscard]] bool has_consistent_values() const {
    return backend_status.is_valid() && rotation_status.is_valid();
  }
};

class ISecretHealthSignalProvider {
 public:
  virtual ~ISecretHealthSignalProvider() = default;

  [[nodiscard]] virtual SecretHealthProbeSignals sample() const = 0;
};

class SecretHealthProbe final : public ISecretHealthSource {
 public:
  explicit SecretHealthProbe(
      std::shared_ptr<ISecretHealthSignalProvider> signal_provider);

  [[nodiscard]] SecretHealthSnapshot sample_secret_health() const override;

 private:
  [[nodiscard]] static bool backend_is_available(
      const SecretBackendStatus& backend_status);
  [[nodiscard]] static bool backend_is_degraded(
      const SecretBackendStatus& backend_status);

  std::shared_ptr<ISecretHealthSignalProvider> signal_provider_;
};

}  // namespace dasall::infra::secret