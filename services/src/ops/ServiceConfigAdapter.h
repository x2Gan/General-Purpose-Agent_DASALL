#pragma once

#include <optional>
#include <string>
#include <vector>

#include "adapters/AdapterRouter.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
struct BuildProfileManifest;
}  // namespace dasall::profiles

namespace dasall::services::internal {

struct ServicePolicyDerivationResult {
  std::optional<ServicePolicyView> policy_view;
  std::string error;

  [[nodiscard]] bool ok() const {
    return policy_view.has_value();
  }
};

class ServiceConfigAdapter {
 public:
  [[nodiscard]] ServicePolicyDerivationResult derive_policy_view(
      const profiles::RuntimePolicySnapshot& runtime_policy,
      const profiles::BuildProfileManifest& build_manifest) const;

 private:
  [[nodiscard]] std::vector<AdapterRouteKind> derive_adapter_preference_order(
      const profiles::BuildProfileManifest& build_manifest) const;
  [[nodiscard]] std::uint32_t derive_command_lane_workers(std::uint32_t worker_threads) const;
  [[nodiscard]] std::uint32_t derive_query_lane_workers(std::uint32_t worker_threads) const;
};

}  // namespace dasall::services::internal