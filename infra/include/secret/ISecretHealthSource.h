#pragma once

#include <cstdint>

namespace dasall::infra::secret {

struct SecretHealthSnapshot {
  bool backend_available = false;
  bool cache_stale = false;
  std::uint64_t active_lease_count = 0;
  std::uint64_t rotation_backlog = 0;
  bool degraded = false;

  [[nodiscard]] bool has_rotation_backlog() const {
    return rotation_backlog > 0;
  }

  [[nodiscard]] bool is_healthy() const {
    return backend_available && !cache_stale && !has_rotation_backlog() && !degraded;
  }
};

class ISecretHealthSource {
 public:
  virtual ~ISecretHealthSource() = default;

  [[nodiscard]] virtual SecretHealthSnapshot sample_secret_health() const = 0;
};

}  // namespace dasall::infra::secret