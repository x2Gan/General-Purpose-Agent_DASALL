#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "AccessTypes.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
}

namespace dasall::access {

struct AccessConfigProjection {
  SnapshotVersionFingerprint fingerprint;
  AccessAuthView auth_view;
  AccessAdmissionView admission_view;
  AccessPublishView publish_view;
  AccessRuntimeGovernanceView runtime_governance_view;
};

struct AccessConfigProjectionResult {
  std::optional<AccessConfigProjection> projection;
  std::string error;

  [[nodiscard]] bool ok() const {
    return projection.has_value() && error.empty();
  }
};

class AccessConfigAdapter {
 public:
  [[nodiscard]] AccessConfigProjectionResult project(
      const AccessBootstrapConfig& bootstrap_config,
      const profiles::RuntimePolicySnapshot& snapshot) const;
  [[nodiscard]] std::optional<AccessConfigProjection> last_known_good_projection() const;
  [[nodiscard]] SnapshotVersionFingerprint snapshot_fingerprint(
      const AccessBootstrapConfig& bootstrap_config,
      const profiles::RuntimePolicySnapshot& snapshot) const;
  [[nodiscard]] bool is_snapshot_current(
      const SnapshotVersionFingerprint& fingerprint,
      const AccessBootstrapConfig& bootstrap_config,
      const profiles::RuntimePolicySnapshot& snapshot) const;

 private:
  [[nodiscard]] AccessConfigProjectionResult project_uncached(
      const AccessBootstrapConfig& bootstrap_config,
      const profiles::RuntimePolicySnapshot& snapshot) const;

  mutable std::mutex cache_mutex_;
  mutable std::optional<SnapshotVersionFingerprint> cached_fingerprint_;
  mutable std::optional<AccessConfigProjection> cached_projection_;
  mutable std::optional<AccessConfigProjection> last_known_good_projection_;
};

}  // namespace dasall::access